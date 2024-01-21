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

/* Include the header file for the CoAP library */
#include <zephyr/net/coap.h>

#include "cJSON.h"

#include "modules/modules_common.h"
#include "events/app_module_event.h"
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"
#include "events/location_module_event.h"

/* Application module super states. */
static enum state_type {
	STATE_DISCONNECTED,
	STATE_CONNECTED,
	STATE_SHUTDOWN,
} state;

struct modem_msg_data {
	union {
		struct app_module_event app;
		struct cloud_module_event cloud;
		struct modem_module_event modem;
		struct location_module_event location;
	} module;
};

#define MSG_Q_SIZE 20

K_MSGQ_DEFINE(msgq_modem, sizeof(struct modem_msg_data), MSG_Q_SIZE, 4);

static K_SEM_DEFINE(lte_connected, 0, 1);

#define MODULE modem_module

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);
static char *state_to_string(enum state_type state)
{
	switch (state)
	{
	case STATE_DISCONNECTED:
		return "STATE_DISCONNECTED";
	case STATE_CONNECTED:
		return "STATE_CONNECTED";
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

static bool app_event_handler(const struct app_event_header *aeh){
	bool consume = false, enqueue_msg = false;
	struct modem_msg_data msg = {0};
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

	// __ASSERT_NO_MSG(false);

	if (enqueue_msg){
		 /* Add the event to the message queue */
        int err = k_msgq_put(&msgq_modem, &msg, K_NO_WAIT);
        if (err) {
            LOG_ERR("Failed to add event to message queue: %d", err);
            /* Handle the error */
        }
	}

	return consume;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
			(evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}
		LOG_INF("Network registration status: %s",
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
				"Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s",
				evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
				"Connected" : "Idle");
		break;
	/* STEP 9.1 - On event PSM update, print PSM paramters and check if was enabled */
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_INF("PSM parameter update: Periodic TAU: %d s, Active time: %d s",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		if (evt->psm_cfg.active_time == -1){
			LOG_ERR("Network rejected PSM parameters. Failed to enable PSM");
		}
		break;
	/* STEP 9.2 - On event eDRX update, print eDRX paramters */
	case LTE_LC_EVT_EDRX_UPDATE:
		LOG_INF("eDRX parameter update: eDRX: %f, PTW: %f",
			evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		break;
	default:
		break;
	}
}

static int modem_configure(void)
{
	int err;

	LOG_INF("Initializing modem library");

	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Failed to initialize the modem library, error: %d", err);
		return err;
	}

	/* STEP 8 - Request PSM and eDRX from the network */
	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("lte_lc_psm_req, error: %d", err);
	}

	err = lte_lc_edrx_req(true);
	if (err) {
		LOG_ERR("lte_lc_edrx_req, error: %d", err);
	}

	LOG_INF("Connecting to LTE network");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Modem could not be configured, error: %d", err);
		return err;
	}

	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");
	dk_set_led_on(DK_LED2);

	struct modem_module_event *modem_module_event = new_modem_module_event();

	modem_module_event->type = MODEM_EVENT_LTE_CONNECTED;

	APP_EVENT_SUBMIT(modem_module_event);

	return 0;
}

static void on_state_disconnected(struct modem_msg_data *msg)
{
	int err;

	if (IS_EVENT(msg, app, APP_EVENT_START)){
		err = modem_configure();
		if (err) {
			LOG_ERR("Failed to configure the modem");
		}
	}

	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_CONNECTED)){
		set_state(STATE_CONNECTED);
	}
}

static void on_state_connected(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_DISCONNECTED)){
		set_state(STATE_DISCONNECTED);
	}
}

int module_thread_fn(void)
{
	int err;
	struct modem_msg_data msg = {0};

	LOG_INF("started!");

	k_sleep(K_SECONDS(3));

	if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LED library");
	}
	LOG_DBG("LEDs initialized");

	while (1) {
        err = k_msgq_get(&msgq_modem, &msg, K_FOREVER);
		if (err) {
            LOG_ERR("Failed to get event from message queue: %d", err);
            /* Handle the error */
        } else {
			switch (state)
			{
			case STATE_DISCONNECTED:
				on_state_disconnected(&msg);
				break;
			case STATE_CONNECTED:
				on_state_connected(&msg);
				break;
			case STATE_SHUTDOWN:
				break;
			
			default:
				LOG_ERR("Unknown state");
				break;
			}
		}
	}

	return 0;
}

K_THREAD_DEFINE(modem_module_thread, 2048,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);
APP_EVENT_SUBSCRIBE(MODULE, location_module_event);