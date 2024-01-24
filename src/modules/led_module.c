#include <zephyr/kernel.h>


#include <caf/events/module_state_event.h>
#include <caf/events/led_event.h>

#include <zephyr/logging/log.h>

#include "configuration/thingy91_nrf9160_ns/led_state_def.h"

#include "modules/modules_common.h"
#include "events/app_module_event.h"
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"
#include "events/location_module_event.h"

/* Application module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_CLOUD_CONNECTING,
	STATE_SHUTDOWN,
} state;

/* Application module sub states. */
static enum sub_state_type {
	SUB_STATE_ACTIVE_MODE,
	SUB_STATE_PASSIVE_MODE,
} sub_state;

/* Application module sub sub states. */
static enum sub_sub_state_type {
	SUB_SUB_STATE_LOCATION_SEARCHING,
	SUB_SUB_STATE_LOCATION_NOT_SEARCHING,
} sub_sub_state;


struct led_msg_data {
	union {
		struct app_module_event app;
		struct cloud_module_event cloud;
		struct modem_module_event modem;
		struct location_module_event location;
	} module;
};

#define MODULE led_module

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

/* Forward declarations*/
static void message_handler(struct led_msg_data *msg);

static char *state_to_string(enum state_type state)
{
	switch (state)
	{
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_CLOUD_CONNECTING:
		return "STATE_CLOUD_CONNECTING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static char *sub_state_to_string(enum sub_state_type state)
{
	switch (state)
	{
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
	default:
		return "Unknown";
	}
}

static char *sub_sub_state_to_string(enum sub_sub_state_type state)
{
	switch (state)
	{
	case SUB_SUB_STATE_LOCATION_SEARCHING:
		return "SUB_SUB_STATE_LOCATION_SEARCHING";
	case SUB_SUB_STATE_LOCATION_NOT_SEARCHING:
		return "SUB_SUB_STATE_LOCATION_NOT_SEARCHING";
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
		LOG_DBG("Sub sub_sub_state: %s", sub_sub_state_to_string(sub_sub_state));
		return;
	}
	LOG_DBG("Sub state transition: %s -> %s", 
		sub_state_to_string(sub_state),
		sub_state_to_string(new_sub_state));
	sub_state = new_sub_state;
}

static void set_sub_sub_state(enum sub_sub_state_type new_sub_sub_state)
{
	if (new_sub_sub_state == sub_sub_state) {
		LOG_DBG("Sub sub state: %s", sub_sub_state_to_string(sub_sub_state));
		return;
	}
	LOG_DBG("Sub sub_sub_state transition: %s -> %s", 
		sub_sub_state_to_string(sub_sub_state),
		sub_sub_state_to_string(new_sub_sub_state));
	sub_sub_state = new_sub_sub_state;
}

static void send_led_event(enum led_id led_id, const struct led_effect *led_effect)
{
	__ASSERT_NO_MSG(led_effect);
	__ASSERT_NO_MSG(led_id < LED_ID_COUNT);

	struct led_event *event = new_led_event();

	event->led_id = led_id;
	event->led_effect = led_effect;
    LOG_DBG("Submitting LED event");
	APP_EVENT_SUBMIT(event);
}



static bool app_event_handler(const struct app_event_header *aeh){
	bool consume = false;
	struct led_msg_data msg = {0};

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

static void on_state_init(struct led_msg_data *msg){

}

static void on_state_running(struct led_msg_data *msg){
    
}

static void on_sub_state_active_mode(struct led_msg_data *msg){
    
}

static void on_sub_state_passive_mode(struct led_msg_data *msg){
    
}

static void on_sub_sub_state_location_searching(struct led_msg_data *msg){
    if (IS_EVENT(msg, location, LOCATION_EVENT_ACTIVE)){
        LOG_DBG("Setting led state to LED_STATE_LOCATION_SEARCHING");
        send_led_event(LED_ID_1, &led_effect[LED_STATE_LOCATION_SEARCHING]);
        set_sub_sub_state(SUB_SUB_STATE_LOCATION_NOT_SEARCHING);
        if (sub_state == SUB_STATE_ACTIVE_MODE){
            send_led_event(LED_ID_1, &led_effect[LED_STATE_ACTIVE_MODE]);
        } else {
            send_led_event(LED_ID_1, &led_effect[LED_STATE_PASSIVE_MODE]);
        }
	}
}

static void on_sub_sub_state_location_not_searching(struct led_msg_data *msg){
    
}

static void on_state_cloud_connecting(struct led_msg_data *msg){
    if (IS_EVENT(msg, cloud, CLOUD_EVENT_SERVER_CONNECTED)){
        LOG_DBG("Setting led state to CLOUD_EVENT_SERVER_CONNECTED");
        if (sub_state == SUB_STATE_ACTIVE_MODE){
            send_led_event(LED_ID_1, &led_effect[LED_STATE_ACTIVE_MODE]);
        } else {
            send_led_event(LED_ID_1, &led_effect[LED_STATE_PASSIVE_MODE]);
        }
        set_state(STATE_RUNNING);
	}
}

static void on_all_states(struct led_msg_data *msg){
    if (IS_EVENT(msg, location, LOCATION_EVENT_ACTIVE)){
        LOG_DBG("Setting led state to LED_STATE_LOCATION_SEARCHING");
        send_led_event(LED_ID_1, &led_effect[LED_STATE_LOCATION_SEARCHING]);
        set_sub_sub_state(SUB_SUB_STATE_LOCATION_SEARCHING);
	}
    // if (IS_EVENT(msg, cloud, CLOUD_EVENT_DATA_SENT)){
    //     LOG_DBG("Setting led state to LED_STATE_CLOUD_SENDING_DATA");
    //     send_led_event(LED_ID_1, &led_effect[LED_STATE_CLOUD_SENDING_DATA]);
	// }
    if (IS_EVENT(msg, cloud, CLOUD_EVENT_SERVER_CONNECTING)){
        LOG_DBG("Setting led state to LED_STATE_CLOUD_CONNECTING");
        send_led_event(LED_ID_1, &led_effect[LED_STATE_CLOUD_CONNECTING]);
        set_state(STATE_CLOUD_CONNECTING);
	}
    if (IS_EVENT(msg, app, APP_EVENT_CONFIG_UPDATE)){
        LOG_DBG("Setting led state to ");
        if (msg->module.app.app_cfg.active_mode){
            send_led_event(LED_ID_1, &led_effect[LED_STATE_CLOUD_CONNECTING]);
            set_sub_state(SUB_STATE_ACTIVE_MODE);
        } else {
            set_sub_state(SUB_STATE_PASSIVE_MODE);
        }
	}
}

static void message_handler(struct led_msg_data *msg){

    switch (state) {
    case STATE_INIT:
        on_state_init(msg);
        break;
    case STATE_RUNNING:
        switch (sub_state) {
        case SUB_STATE_ACTIVE_MODE:
            switch (sub_sub_state) {
            case SUB_SUB_STATE_LOCATION_SEARCHING:
                on_sub_sub_state_location_searching(msg);
                break;
            case SUB_SUB_STATE_LOCATION_NOT_SEARCHING:
                on_sub_sub_state_location_not_searching(msg);
                break;
            }
            on_sub_state_active_mode(msg);
            break;
        case SUB_STATE_PASSIVE_MODE:
            switch (sub_sub_state) {
            case SUB_SUB_STATE_LOCATION_SEARCHING:
                on_sub_sub_state_location_searching(msg);
                break;
            case SUB_SUB_STATE_LOCATION_NOT_SEARCHING:
                on_sub_sub_state_location_not_searching(msg);
                break;
            }
            on_sub_state_passive_mode(msg);
            break;
        }
        on_state_running(msg);
        break;
    case STATE_CLOUD_CONNECTING:
        on_state_cloud_connecting(msg);
        break;
    case STATE_SHUTDOWN:
        // Do nothing
        break;
    }
	on_all_states(msg);
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);
APP_EVENT_SUBSCRIBE(MODULE, location_module_event);