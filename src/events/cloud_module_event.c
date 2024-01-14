#include "events/cloud_module_event.h"

static void profile_cloud_module_event(struct log_event_buf *buf,
				 const struct app_event_header *aeh)
{
}

static void log_cloud_module_event(const struct app_event_header *aeh)
{
	struct cloud_module_event *event = cast_cloud_module_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "cloud_module_event: type:%d", event->type);
}

APP_EVENT_INFO_DEFINE(cloud_module_event,
                      ENCODE(),
                      ENCODE(),
                      profile_cloud_module_event);

APP_EVENT_TYPE_DEFINE(cloud_module_event,
                log_cloud_module_event,
                &cloud_module_event_info,
                APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));