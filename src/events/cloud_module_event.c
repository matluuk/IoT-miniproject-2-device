#include "events/cloud_module_event.h"

const char *get_cloud_module_event_type_str(enum cloud_module_event_type type)
{
    switch (type) {
        case CLOUD_EVENT_INTIALIZED:
            return "CLOUD_EVENT_INTIALIZED";
        case CLOUD_EVENT_SERVER_CONNECTED:
            return "CLOUD_EVENT_SERVER_CONNECTED";
        case CLOUD_EVENT_SERVER_DISCONNECTED:
            return "CLOUD_EVENT_SERVER_DISCONNECTED";
        case CLOUD_EVENT_SERVER_CONNECTING:
            return "CLOUD_EVENT_SERVER_CONNECTING";
        case CLOUD_EVENT_BUTTON_PRESSED:
            return "CLOUD_EVENT_BUTTON_PRESSED";
        case CLOUD_EVENT_DATA_SENT:
            return "CLOUD_EVENT_DATA_SENT";
        case CLOUD_EVENT_CLOUD_CONFIG_RECEIVED:
            return "CLOUD_EVENT_CLOUD_CONFIG_RECEIVED";
        default:
            return "UNKNOWN_EVENT_TYPE";
    }
}

static void profile_cloud_module_event(struct log_event_buf *buf,
				 const struct app_event_header *aeh)
{
}

static void log_cloud_module_event(const struct app_event_header *aeh)
{
	struct cloud_module_event *event = cast_cloud_module_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "cloud_module_event: %s", get_cloud_module_event_type_str(event->type));
}

APP_EVENT_INFO_DEFINE(cloud_module_event,
                      ENCODE(),
                      ENCODE(),
                      profile_cloud_module_event);

APP_EVENT_TYPE_DEFINE(cloud_module_event,
                log_cloud_module_event,
                &cloud_module_event_info,
                APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));