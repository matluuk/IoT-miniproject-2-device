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

#include "events/app_module_event.h"
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"
#include "events/location_module_event.h"

/* Application module super states. */
static enum state_type {
	STATE_DISCONNECTED,
	STATE_CONNECTED,
	STATE_CONNECTING,
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

// static struct nrf_modem_gnss_pvt_data_frame pvt_data;

// static int64_t gnss_start_time;
// static bool first_fix = false;

/* STEP 4.2 - Define the macros for the CoAP version and message length */
// #define APP_COAP_VERSION 1
// #define APP_COAP_MAX_MSG_LEN 1280

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
	case STATE_CONNECTING:
		return "STATE_CONNECTING";
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

// static void print_fix_data(struct nrf_modem_gnss_pvt_data_frame *pvt_data)
// {
// 	LOG_INF("Latitude:       %.06f", pvt_data->latitude);
// 	LOG_INF("Longitude:      %.06f", pvt_data->longitude);
// 	LOG_INF("Altitude:       %.01f m", pvt_data->altitude);
// 	LOG_INF("Time (UTC):     %02u:%02u:%02u.%03u",
// 	       pvt_data->datetime.hour,
// 	       pvt_data->datetime.minute,
// 	       pvt_data->datetime.seconds,
// 	       pvt_data->datetime.ms);
// }

// static void gnss_event_handler(int event)
// {
// 	int err, num_satellites;

// 	switch (event) {
// 	case NRF_MODEM_GNSS_EVT_PVT:
// 		num_satellites = 0;
// 		for (int i = 0; i < 12 ; i++) {
// 			if (pvt_data.sv[i].signal != 0) {
// 				num_satellites++;
// 			}
// 		}
// 		LOG_INF("Searching. Current satellites: %d", num_satellites);
// 		err = nrf_modem_gnss_read(&pvt_data, sizeof(pvt_data), NRF_MODEM_GNSS_DATA_PVT);
// 		if (err) {
// 			LOG_ERR("nrf_modem_gnss_read failed, err %d", err);
// 			return;
// 		}
// 		if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
// 			dk_set_led_on(DK_LED1);
// 			print_fix_data(&pvt_data);
// 			if (!first_fix) {
// 				LOG_INF("Time to first fix: %2.1lld s", (k_uptime_get() - gnss_start_time)/1000);
// 				first_fix = true;
// 			}
// 			return;
// 		}
// 		/* STEP 5 - Check for the flags indicating GNSS is blocked */
// 		if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
// 			LOG_INF("GNSS blocked by LTE activity");
// 		} else if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
// 			LOG_INF("Insufficient GNSS time windows");
// 		}
// 		break;

// 	case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
// 		LOG_INF("GNSS has woken up");
// 		break;
// 	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
// 		LOG_INF("GNSS enter sleep after fix");
// 		break;
// 	default:
// 		break;
// 	}
// }

// static int gnss_init_and_start(void)
// {

// 	/* STEP 4 - Set the modem mode to normal */
// 	if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_NORMAL) != 0) {
// 		LOG_ERR("Failed to activate GNSS functional mode");
// 		return -1;
// 	}

// 	if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
// 		LOG_ERR("Failed to set GNSS event handler");
// 		return -1;
// 	}

// 	if (nrf_modem_gnss_fix_interval_set(CONFIG_GNSS_PERIODIC_INTERVAL) != 0) {
// 		LOG_ERR("Failed to set GNSS fix interval");
// 		return -1;
// 	}

// 	if (nrf_modem_gnss_fix_retry_set(CONFIG_GNSS_PERIODIC_TIMEOUT) != 0) {
// 		LOG_ERR("Failed to set GNSS fix retry");
// 		return -1;
// 	}

// 	LOG_INF("Starting GNSS");
// 	if (nrf_modem_gnss_start() != 0) {
// 		LOG_ERR("Failed to start GNSS");
// 		return -1;
// 	}

// 	gnss_start_time = k_uptime_get();

// 	LOG_INF("GNSS started");

// 	return 0;
// }

static void on_state_disconnected(struct modem_msg_data *msg)
{
	int err;

	if (msg->module.app.type == APP_EVENT_START) {
		err = modem_configure();
		if (err) {
			LOG_ERR("Failed to configure the modem");
		}
		// set_state(STATE_CONNECTING);
	}

	if (msg->module.modem.type == MODEM_EVENT_LTE_CONNECTED) {
		set_state(STATE_CONNECTED);

		// if (gnss_init_and_start() != 0) {
		// 	LOG_ERR("Failed to initialize and start GNSS");
		// }
	}
}

static void on_state_connecting(struct modem_msg_data *msg)
{
	// if (msg->module.modem.type == MODEM_EVENT_LTE_CONNECTED) {
	// 	set_state(STATE_CONNECTED);

	// 	if (gnss_init_and_start() != 0) {
	// 		LOG_ERR("Failed to initialize and start GNSS");
	// 	}
	// }
}

static void on_state_connected(struct modem_msg_data *msg)
{
	if (msg->module.modem.type == MODEM_EVENT_LTE_DISCONNECTED) {
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
			case STATE_CONNECTING:
				break;
				on_state_connecting(&msg);
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

// K_THREAD_DEFINE(modem_module_thread, CONFIG_MODEM_THREAD_STACK_SIZE,
// 		module_thread_fn, NULL, NULL, NULL,
// 		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);


K_THREAD_DEFINE(modem_module_thread, 2048,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);
APP_EVENT_SUBSCRIBE(MODULE, location_module_event);