#include "events/app_module_event.h"

const char *get_app_module_event_type_str(enum app_module_event_type type)
{
    switch (type) {
        case APP_EVENT_START:
            return "APP_EVENT_START";
        case APP_EVENT_LTE_CONNECT:
            return "APP_EVENT_LTE_CONNECT";
        case APP_EVENT_LTE_DISCONNECT:
            return "APP_EVENT_LTE_DISCONNECT";
        case APP_EVENT_LOCATION_GET:
            return "APP_EVENT_LOCATION_GET";
        case APP_EVENT_BATTERY_GET:
            return "APP_EVENT_BATTERY_GET";
        case APP_EVENT_CONFIG_GET:
            return "APP_EVENT_CONFIG_GET";
        case APP_EVENT_CONFIG_UPDATE:
            return "APP_EVENT_CONFIG_UPDATE";
        case APP_EVENT_START_MOVEMENT:
            return "APP_EVENT_START_MOVEMENT";
        default:
            return "UNKNOWN_EVENT_TYPE";
    }
}

static void profile_app_module_event(struct log_event_buf *buf,
				 const struct app_event_header *aeh)
{
}

static void log_app_module_event(const struct app_event_header *aeh)
{
	struct app_module_event *event = cast_app_module_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "app_module_event: %s", get_app_module_event_type_str(event->type));
}

APP_EVENT_INFO_DEFINE(app_module_event,
                      ENCODE(),
                      ENCODE(),
                      profile_app_module_event);

APP_EVENT_TYPE_DEFINE(app_module_event,
                log_app_module_event,
                &app_module_event_info,
                APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));