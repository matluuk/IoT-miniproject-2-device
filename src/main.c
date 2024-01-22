#include <stdio.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/sys/reboot.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

#include "codec.h"

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#define MODULE app_module

#include "modules/modules_common.h"
#include "events/app_module_event.h"
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"
#include "events/location_module_event.h"

/* Application module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

/* Application sub states. The application can be in either active or passive
 * mode.
 *
 * Active mode: Sensor GNSS position is acquired at a configured
 *		interval and sent to cloud.
 *
 * Passive mode: Sensor GNSS position is acquired when movement is
 *		 detected, or after the configured movement timeout occurs.
 *		 Movement detection is not yet implemented.
 */
static enum sub_state_type {
	SUB_STATE_ACTIVE_MODE,
	SUB_STATE_PASSIVE_MODE,
} sub_state;

static struct app_cfg current_cfg = {
	.device_id = 0,
	.active_mode = true,
	.location_timeout = 300,
	.active_wait_timeout = 120,
	.passive_wait_timeout = 3600
};

struct app_msg_data {
	union {
		struct app_module_event app;
		struct cloud_module_event cloud;
		struct modem_module_event modem;
		struct location_module_event location;
	} module;
};

static void data_sample_timer_handler(struct k_timer *timer_id);

#define MSG_Q_SIZE 20

K_MSGQ_DEFINE(msgq_app, sizeof(struct app_msg_data), MSG_Q_SIZE, 4);

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

/* Data sample timer used in ative mode*/
K_TIMER_DEFINE(data_sample_timer, data_sample_timer_handler, NULL);

static char *sub_state_to_string(enum sub_state_type sub_state)
{
	switch (sub_state)
	{
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
	default:
		return "Unknown";
	}
}

static char *state_to_string(enum state_type state)
{
	switch (state)
	{
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static void set_state(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state_to_string(state));
		return;
	}
	LOG_DBG("State transition: %s -> %s", 
		state_to_string(state),
		state_to_string(new_state));
	state = new_state;
}

static void set_sub_state(enum sub_state_type new_sub_state)
{
	if (new_sub_state == sub_state) {
		LOG_DBG("Sub state: %s", state_to_string(sub_state));
		return;
	}
	LOG_DBG("Sub state transition: %s -> %s", 
		sub_state_to_string(sub_state),
		sub_state_to_string(new_sub_state));
	sub_state = new_sub_state;
}

static void set_passive_mode_timer(void)
{
	LOG_INF("Setting passive mode timer to %ds", current_cfg.passive_wait_timeout);
	k_timer_start(&data_sample_timer, K_SECONDS(current_cfg.passive_wait_timeout), K_SECONDS(current_cfg.passive_wait_timeout));
}

static void set_active_mode_timer(void)
{
	LOG_INF("Setting active mode timer to %ds", current_cfg.active_wait_timeout);
	k_timer_start(&data_sample_timer, K_SECONDS(current_cfg.active_wait_timeout), K_SECONDS(current_cfg.active_wait_timeout));
}

static bool app_event_handler(const struct app_event_header *aeh){
	bool consume = false, enqueue_msg = false;
	struct app_msg_data msg = {0};
	if (is_app_module_event(aeh)){
		struct app_module_event *event = cast_app_module_event(aeh);
		msg.module.app = *event;
		enqueue_msg = true;
	}
	
	if (is_cloud_module_event(aeh)){
		struct cloud_module_event *event = cast_cloud_module_event(aeh);
		msg.module.cloud = *event;
		enqueue_msg = true;
	}
	
	if (is_modem_module_event(aeh)){
		struct modem_module_event *event = cast_modem_module_event(aeh);
		msg.module.modem = *event;
		enqueue_msg = true;
	}
	
	if (is_location_module_event(aeh)){
		struct location_module_event *event = cast_location_module_event(aeh);
		msg.module.location = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg){
		 /* Add the event to the message queue */
        int err = k_msgq_put(&msgq_app, &msg, K_NO_WAIT);
        if (err) {
            LOG_ERR("Failed to add event to message queue: %d", err);
            /* Handle the error */
        }
	}

	return consume;
}

static void data_sample_timer_handler(struct k_timer *timer_id)
{
	LOG_INF("Data sample timer expired");
	struct app_module_event *app_module_event = new_app_module_event();
	app_module_event->type = APP_EVENT_LOCATION_GET;
	APP_EVENT_SUBMIT(app_module_event);
	if (current_cfg.active_mode) {
		set_active_mode_timer();
	} else {
		set_passive_mode_timer();
	}
}

static void handle_new_config(struct app_cfg *new_cfg){
	bool config_change = false;
	if (current_cfg.active_mode != new_cfg->active_mode){
		current_cfg.active_mode = new_cfg->active_mode;
		if (current_cfg.active_mode){
			LOG_DBG("New Device mode: Active");
		} else {
			LOG_DBG("New Device mode: Passive");
		}
		config_change = true;
	}

	if (new_cfg->active_wait_timeout > 0){
		if (current_cfg.active_wait_timeout != new_cfg->active_wait_timeout){
			current_cfg.active_wait_timeout = new_cfg->active_wait_timeout;
			LOG_DBG("New active wait timeout: %d", current_cfg.active_wait_timeout);
			config_change = true;
		}
	} else {
		LOG_WRN("New active wait timeout out of range: %d", new_cfg->active_wait_timeout);
	}

	if (new_cfg->location_timeout > 0){
		if (current_cfg.location_timeout != new_cfg->location_timeout){
			current_cfg.location_timeout = new_cfg->location_timeout;
			LOG_DBG("New location timeout: %d", current_cfg.location_timeout);
			config_change = true;
		}
	} else {
		LOG_WRN("New location timeout out of range: %d", new_cfg->location_timeout);
	}

	if (new_cfg->passive_wait_timeout > 0){
		if (current_cfg.passive_wait_timeout != new_cfg->passive_wait_timeout){
			current_cfg.passive_wait_timeout = new_cfg->passive_wait_timeout;
			LOG_DBG("New passive wait timeout: %d", current_cfg.passive_wait_timeout);
			config_change = true;
		}
	} else {
		LOG_WRN("New passive wait timeout out of range: %d", new_cfg->passive_wait_timeout);
	}

	if (config_change){
		//TODO Save config to flash

		struct app_module_event *app_module_event = new_app_module_event();
		app_module_event->type = APP_EVENT_CONFIG_UPDATE;
		app_module_event->app_cfg = current_cfg;
		APP_EVENT_SUBMIT(app_module_event);
	}
}

static void on_state_init(struct app_msg_data *msg)
{
	set_state(STATE_RUNNING);
	if (current_cfg.active_mode) {
		set_sub_state(SUB_STATE_ACTIVE_MODE);
		set_active_mode_timer();
	} else {
		set_sub_state(SUB_STATE_PASSIVE_MODE);
		set_passive_mode_timer();
	}
}

static void on_state_running(struct app_msg_data *msg)
{	
	// flag used to trigger data request, when connected to the cloud fpr the first time
	static bool initial_data_request;

	if (IS_EVENT(msg, cloud, CLOUD_EVENT_SERVER_CONNECTED) && !initial_data_request){
		struct app_module_event *app_module_event = new_app_module_event();
		app_module_event->type = APP_EVENT_LOCATION_GET;
		APP_EVENT_SUBMIT(app_module_event);

		initial_data_request = true;
	}
}

static void on_sub_state_active(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVENT_CONFIG_UPDATE)){
		if (current_cfg.active_mode){
			set_active_mode_timer();
			return;
		}
		set_passive_mode_timer();
		set_sub_state(SUB_STATE_PASSIVE_MODE);
	}
}

static void on_sub_state_passive(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVENT_CONFIG_UPDATE)){
		if (current_cfg.active_mode){
			set_active_mode_timer();
			set_sub_state(SUB_STATE_ACTIVE_MODE);
			return;
		}
		set_passive_mode_timer();
	}
}

static void on_all_states(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVENT_CLOUD_CONFIG_RECEIVED)){
		struct app_cfg new_cfg = msg->module.cloud.cloud_cfg;
		handle_new_config(&new_cfg);
	}
}

int main(void)
{	
	int err;
	struct app_msg_data msg = {0};

	k_sleep(K_SECONDS(3));

	LOG_INF("Application started");

	if (app_event_manager_init()) {
		LOG_ERR("Application Event Manager could not be initialized, rebooting...");
		k_sleep(K_SECONDS(5));
		sys_reboot(SYS_REBOOT_COLD);
	} else {
		LOG_INF("Application Event Manager initialized");
		struct app_module_event *app_module_event = new_app_module_event();
		app_module_event->type = APP_EVENT_START;
		app_module_event->app_cfg = current_cfg;
		APP_EVENT_SUBMIT(app_module_event);
	}

	while (1)
	{	
        err = k_msgq_get(&msgq_app, &msg, K_FOREVER);
		if (err) {
            LOG_ERR("Failed to get event from message queue: %d", err);
            /* Handle the error */
        } else {

			switch (state)
			{
			case STATE_INIT:
				on_state_init(&msg);
				break;
			case STATE_RUNNING:
				switch (sub_state)
				{
				case SUB_STATE_ACTIVE_MODE:
					on_sub_state_active(&msg);
					break;
				case SUB_STATE_PASSIVE_MODE:
					on_sub_state_passive(&msg);
					break;
				default:
					break;
				on_state_running(&msg);
				}
				break;
			case STATE_SHUTDOWN:
				break;
			
			default:
				LOG_ERR("Unknown state");
				break;
			}
			on_all_states(&msg);
		}
	}

	return 0;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);
APP_EVENT_SUBSCRIBE(MODULE, location_module_event);