#include <stdio.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>


#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include <events/app_module_event.h>
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"

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
 */
static enum sub_state_type {
	SUB_STATE_ACTIVE_MODE,
	SUB_STATE_PASSIVE_MODE,
} sub_state;

#define MODULE app_module

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

struct app_msg_data {
	union {
		struct app_module_event app;
		struct cloud_module_event cloud;
		struct modem_module_event modem;
	} module;
};

#define MSG_Q_SIZE 20

K_MSGQ_DEFINE(msgq_app, sizeof(struct app_msg_data), MSG_Q_SIZE, 4);

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

static bool app_event_handler(const struct app_event_header *aeh){
	bool consume = false, enqueue_msg = false;
	struct app_msg_data msg = {0};
	if (is_app_module_event(aeh)){
		struct app_module_event *event = cast_app_module_event(aeh);
		msg.module.app = *event;
	}
	
	if (is_cloud_module_event(aeh)){
		struct cloud_module_event *event = cast_cloud_module_event(aeh);
		msg.module.cloud = *event;
	}

	
	if (is_modem_module_event(aeh)){
		struct modem_module_event *event = cast_modem_module_event(aeh);
		msg.module.modem = *event;
	}

	// __ASSERT_NO_MSG(false);

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

static void on_state_init(struct app_msg_data *msg)
{
	set_state(STATE_RUNNING);
	set_sub_state(SUB_STATE_ACTIVE_MODE);
}

static void on_state_running(struct app_msg_data *msg)
{
	
}

static void on_sub_state_active(struct app_msg_data *msg)
{
	
}

static void on_sub_state_passive(struct app_msg_data *msg)
{
	
}

int main(void)
{	
	int err;

	if (app_event_manager_init()) {
		LOG_ERR("Application Event Manager not initialized");
	}

	struct app_module_event *app_module_event = new_app_module_event();
	app_module_event->type = APP_EVENT_START;
	APP_EVENT_SUBMIT(app_module_event);

	while (1)
	{	
		struct app_msg_data msg = {0};
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
		}
		// k_sleep(K_SECONDS(20));
	}

	return 0;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);