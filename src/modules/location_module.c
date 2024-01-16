/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <nrf_modem_at.h>
#include <modem/lte_lc.h>
#include <modem/location.h>
#include <modem/nrf_modem_lib.h>
#include <date_time.h>
#include <nrf_modem_gnss.h>

#include "events/app_module_event.h"
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"
#include "events/location_module_event.h"

static K_SEM_DEFINE(location_event, 0, 1);

static K_SEM_DEFINE(time_update_finished, 0, 1);

#define MODULE location_module

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

struct location_msg_data {
	union {
		struct app_module_event app;
		struct cloud_module_event cloud;
		struct modem_module_event modem;
		struct location_module_event location;
	} module;
};

static enum state_type {
    STATE_INIT,
    STATE_RUNNING,
    STATE_SHUTDOWN,
} state;

static enum sub_state_type {
    SUB_STATE_IDLE,
    SUB_STATE_SEARCHING,
} sub_state;

static struct nrf_modem_gnss_pvt_data_frame pvt_data;

/* Forward declarations*/
static void message_handler(struct location_msg_data *msg);

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

static char *sub_state_to_string(enum sub_state_type sub_state)
{
    switch (sub_state)
    {
    case SUB_STATE_IDLE:
        return "SUB_STATE_IDLE";
    case SUB_STATE_SEARCHING:
        return "SUB_STATE_SEARCHING";
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
        LOG_DBG("Sub state: %s", sub_state_to_string(sub_state));
        return;
    }
    LOG_DBG("Sub state transition: %s -> %s", 
        sub_state_to_string(sub_state),
        sub_state_to_string(new_sub_state));
    sub_state = new_sub_state;
}

static bool app_event_handler(const struct app_event_header *aeh){
	LOG_INF("App event handler");
	bool consume = false;
	struct location_msg_data msg = {0};
	if (is_app_module_event(aeh)){
		struct app_module_event *event = cast_app_module_event(aeh);
		msg.module.app = *event;
        message_handler(&msg);
	}
	
	if (is_cloud_module_event(aeh)){
		struct cloud_module_event *event = cast_cloud_module_event(aeh);
		msg.module.cloud = *event;
        message_handler(&msg);
    }

	
	if (is_modem_module_event(aeh)){
		struct modem_module_event *event = cast_modem_module_event(aeh);
		msg.module.modem = *event;
        message_handler(&msg);
	}

	
	if (is_location_module_event(aeh)){
		struct location_module_event *event = cast_location_module_event(aeh);
		msg.module.location = *event;
        message_handler(&msg);
	}

	return consume;
}

static void date_time_evt_handler(const struct date_time_evt *evt)
{
	k_sem_give(&time_update_finished);
}

static void time_set(void)
{
	/* Change datetime.year and datetime.month to accommodate the
	 * correct input format.
	 */
	struct tm gnss_time = {
		.tm_year = pvt_data.datetime.year - 1900,
		.tm_mon = pvt_data.datetime.month - 1,
		.tm_mday = pvt_data.datetime.day,
		.tm_hour = pvt_data.datetime.hour,
		.tm_min = pvt_data.datetime.minute,
		.tm_sec = pvt_data.datetime.seconds,
	};

	date_time_set(&gnss_time);
}

static void send_pvt_data(){
    
        struct location_module_event *location_module_event = new_location_module_event();

        location_module_event->type = LOCATION_EVENT_GNSS_DATA_READY;
        location_module_event->location.pvt.latitude = pvt_data.latitude;
        location_module_event->location.pvt.longitude = pvt_data.longitude;
        location_module_event->location.pvt.accuracy = pvt_data.accuracy;
        location_module_event->location.pvt.altitude = pvt_data.altitude;
        location_module_event->location.pvt.speed = pvt_data.speed;
        location_module_event->location.pvt.heading = pvt_data.heading;
        // location_module_event->location.satellites_tracked =
		// 		event_data->location.details.gnss.satellites_tracked
        location_module_event->location.timestamp = k_uptime_get();


        APP_EVENT_SUBMIT(location_module_event);
}

static void location_event_handler(const struct location_event_data *event_data)
{
	switch (event_data->id) {
	case LOCATION_EVT_LOCATION:
		LOG_DBG("Got location:");
		LOG_DBG("  method: %s", location_method_str(event_data->method));
		LOG_DBG("  latitude: %.06f", event_data->location.latitude);
		LOG_DBG("  longitude: %.06f", event_data->location.longitude);
		LOG_DBG("  accuracy: %.01f m", event_data->location.accuracy);
		LOG_DBG("  altitude: %.01f m", event_data->location.details.gnss.pvt_data.altitude);
		LOG_DBG("  speed: %.01f m", event_data->location.details.gnss.pvt_data.speed);
		LOG_DBG("  heading: %.01f deg", event_data->location.details.gnss.pvt_data.heading);

		if (event_data->location.datetime.valid) {
			LOG_DBG("  date: %04d-%02d-%02d",
				event_data->location.datetime.year,
				event_data->location.datetime.month,
				event_data->location.datetime.day);
			LOG_DBG("  time: %02d:%02d:%02d.%03d UTC",
				event_data->location.datetime.hour,
				event_data->location.datetime.minute,
				event_data->location.datetime.second,
				event_data->location.datetime.ms);
		}
		LOG_INF("  Google maps URL: https://maps.google.com/?q=%.06f,%.06f\n\n",
			event_data->location.latitude, event_data->location.longitude);

        pvt_data = event_data->location.details.gnss.pvt_data;

        if (event_data->location.datetime.valid) {
            /* Date and time is in pvt_data that is set above */
            time_set();
        }
        
        send_pvt_data();
		break;

	case LOCATION_EVT_TIMEOUT:
		LOG_INF("Getting location timed out\n\n");
		break;

	case LOCATION_EVT_ERROR:
		LOG_ERR("Getting location failed\n\n");
		break;

	case LOCATION_EVT_GNSS_ASSISTANCE_REQUEST:
		LOG_INF("Getting location assistance requested (A-GNSS). Not doing anything.\n\n");
		break;

	case LOCATION_EVT_GNSS_PREDICTION_REQUEST:
		LOG_INF("Getting location assistance requested (P-GPS). Not doing anything.\n\n");
		break;

	default:
		LOG_ERR("Getting location: Unknown event\n\n");
		break;
	}

	k_sem_give(&location_event);
}

static void location_event_wait(void)
{
	k_sem_take(&location_event, K_FOREVER);
}

/**
 * @brief Retrieve location so that fallback is applied.
 *
 * @details This is achieved by setting GNSS as first priority method and giving it too short
 * timeout. Then a fallback to next method, which is cellular in this example, occurs.
 */
static void location_with_fallback_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {LOCATION_METHOD_GNSS, LOCATION_METHOD_CELLULAR};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);
	/* GNSS timeout is set to 1 second to force a failure. */
	config.methods[0].gnss.timeout = 1 * MSEC_PER_SEC;
	/* Default cellular configuration may be overridden here. */
	config.methods[1].cellular.timeout = 40 * MSEC_PER_SEC;

	LOG_INF("Requesting location with short GNSS timeout to trigger fallback to cellular...\n");

	err = location_request(&config);
	if (err) {
		LOG_ERR("Requesting location failed, error: %d\n", err);
		return;
	}

	location_event_wait();
}

/**
 * @brief Retrieve location with default configuration.
 *
 * @details This is achieved by not passing configuration at all to location_request().
 */
static void location_default_get(void)
{
	int err;

	LOG_INF("Requesting location with the default configuration...\n");

	err = location_request(NULL);
	if (err) {
		LOG_ERR("Requesting location failed, error: %d\n", err);
		return;
	}

	location_event_wait();
}

/**
 * @brief Retrieve location with GNSS low accuracy.
 */
static void location_gnss_low_accuracy_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {LOCATION_METHOD_GNSS};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);
	config.methods[0].gnss.accuracy = LOCATION_ACCURACY_LOW;

	LOG_INF("Requesting low accuracy GNSS location...\n");

	err = location_request(&config);
	if (err) {
		LOG_ERR("Requesting location failed, error: %d\n", err);
		return;
	}

	location_event_wait();
}

/**
 * @brief Retrieve location with GNSS high accuracy.
 */
static void location_gnss_high_accuracy_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {LOCATION_METHOD_GNSS};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);
	config.methods[0].gnss.accuracy = LOCATION_ACCURACY_HIGH;

	LOG_INF("Requesting high accuracy GNSS location...\n");

	err = location_request(&config);
	if (err) {
		LOG_ERR("Requesting location failed, error: %d\n", err);
		return;
	}

	location_event_wait();
}

#if defined(CONFIG_LOCATION_METHOD_WIFI)
/**
 * @brief Retrieve location with Wi-Fi positioning as first priority, GNSS as second
 * and cellular as third.
 */
static void location_wifi_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {
		LOCATION_METHOD_WIFI,
		LOCATION_METHOD_GNSS,
		LOCATION_METHOD_CELLULAR};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);

	LOG_INF("Requesting Wi-Fi location with GNSS and cellular fallback...\n");

	err = location_request(&config);
	if (err) {
		LOG_INF("Requesting location failed, error: %d\n", err);
		return;
	}

	location_event_wait();
}
#endif

/**
 * @brief Retrieve location periodically with GNSS as first priority and cellular as second.
 */
static void location_gnss_periodic_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {LOCATION_METHOD_GNSS, LOCATION_METHOD_CELLULAR};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);
	config.interval = 30;

	LOG_INF("Requesting 30s periodic GNSS location with cellular fallback...\n");

	err = location_request(&config);
	if (err) {
		LOG_ERR("Requesting location failed, error: %d\n", err);
		return;
	}
}

// int main(void)
// {
// 	int err;

// 	LOG_INF("Location sample started\n\n");

// 	// err = nrf_modem_lib_init();
// 	// if (err) {
// 	// 	LOG_INF("Modem library initialization failed, error: %d\n", err);
// 	// 	return err;
// 	// }

// 	if (IS_ENABLED(CONFIG_DATE_TIME)) {
// 		/* Registering early for date_time event handler to avoid missing
// 		 * the first event after LTE is connected.
// 		 */
// 		date_time_register_handler(date_time_evt_handler);
// 	}

// 	/* A-GNSS/P-GPS needs to know the current time. */
// 	if (IS_ENABLED(CONFIG_DATE_TIME)) {
// 		LOG_INF("Waiting for current time\n");

// 		/* Wait for an event from the Date Time library. */
// 		k_sem_take(&time_update_finished, K_MINUTES(10));

// 		if (!date_time_is_valid()) {
// 			LOG_INF("Failed to get current time. Continuing anyway.\n");
// 		}
// 	}

// 	// err = location_init(location_event_handler);
// 	// if (err) {
// 	// 	LOG_INF("Initializing the Location library failed, error: %d\n", err);
// 	// 	return -1;
// 	// }

// 	/* The fallback case is run first, otherwise GNSS might get a fix even with a 1 second
// 	 * timeout.
// 	 */
// 	location_with_fallback_get();

// 	location_default_get();

// 	location_gnss_low_accuracy_get();

// 	location_gnss_high_accuracy_get();

// #if defined(CONFIG_LOCATION_METHOD_WIFI)
// 	location_wifi_get();
// #endif

// 	location_gnss_periodic_get();

// 	return 0;
// }

static void on_state_init(struct location_msg_data *msg){
    int err;
    if (msg->module.modem.type == MODEM_EVENT_LTE_CONNECTED){
        err = location_init(location_event_handler);
        if (err) {
            LOG_ERR("Initializing the Location library failed, error: %d\n", err);
        }
        if (IS_ENABLED(CONFIG_DATE_TIME)) {
            /* Registering early for date_time event handler to avoid missing
            * the first event after LTE is connected.
            */
            date_time_register_handler(date_time_evt_handler);
        }
        set_state(STATE_RUNNING);
        set_sub_state(SUB_STATE_IDLE);
    }
}

static void on_state_running(struct location_msg_data *msg){

}

static void on_sub_state_idle(struct location_msg_data *msg){
    if (msg->module.app.type == APP_EVENT_LOCATION_GET){
        location_default_get();
    }
}

static void on_sub_state_searching(struct location_msg_data *msg){

}


static void message_handler(struct location_msg_data *msg){

    switch (state) {
        case STATE_INIT:
            on_state_init(msg);
            break;
        case STATE_RUNNING:
            switch (sub_state) {
                case SUB_STATE_SEARCHING:
                    on_sub_state_searching(msg);
                    break;

                case SUB_STATE_IDLE:
                    on_sub_state_idle(msg);
                    break;
            }
            on_state_running(msg);
            break;

        case STATE_SHUTDOWN:
            // Do nothing
            break;
    }

}


APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);
APP_EVENT_SUBSCRIBE(MODULE, location_module_event);