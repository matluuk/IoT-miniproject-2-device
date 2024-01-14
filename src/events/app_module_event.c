#include "events/app_module_event.h"

static void profile_app_module_event(struct log_event_buf *buf,
				 const struct app_event_header *aeh)
{
}

static void log_app_module_event(const struct app_event_header *aeh)
{
	struct app_module_event *event = cast_app_module_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "app_module_event: type:%d", event->type);
}

APP_EVENT_INFO_DEFINE(app_module_event,
                      ENCODE(),
                      ENCODE(),
                      profile_app_module_event);

APP_EVENT_TYPE_DEFINE(app_module_event,
                log_app_module_event,
                &app_module_event_info,
                APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));