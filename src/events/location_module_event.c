#include "events/location_module_event.h"

const char *get_location_module_event_type_str(enum location_module_event_type type)
{
    switch (type) {
        case LOCATION_EVENT_GNSS_DATA_READY:
            return "LOCATION_EVENT_GNSS_DATA_READY";
        case LOCATION_EVENT_TIMEOUT:
            return "LOCATION_EVENT_TIMEOUT";
        case LOCATION_EVENT_ACTIVE:
            return "LOCATION_EVENT_ACTIVE";
        case LOCATION_EVENT_INACTIVE:
            return "LOCATION_EVENT_INACTIVE";
        default:
            return "UNKNOWN_EVENT_TYPE";
    }
}

static void profile_location_module_event(struct log_event_buf *buf,
                                          const struct app_event_header *aeh)
{
}

static void log_location_module_event(const struct app_event_header *aeh)
{
    struct location_module_event *event = cast_location_module_event(aeh);

    APP_EVENT_MANAGER_LOG(aeh, "location_module_event: %s", get_location_module_event_type_str(event->type));
}

APP_EVENT_INFO_DEFINE(location_module_event,
                      ENCODE(),
                      ENCODE(),
                      profile_location_module_event);

APP_EVENT_TYPE_DEFINE(location_module_event,
                      log_location_module_event,
                      &location_module_event_info,
                      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));