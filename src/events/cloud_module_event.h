#ifndef _CLOUD_MODULE_EVENT_H_
#define _CLOUD_MODULE_EVENT_H_

/**
 * @brief cloud module event
 * @defgroup cloud_module_event cloud module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @brief cloud event types. */
enum cloud_module_event_type {
    CLOUD_EVENT_INTIALIZED,
    CLOUD_EVENT_SERVER_CONNECTED,
    CLOUD_EVENT_SERVER_DISCONNECTED,
    CLOUD_EVENT_SERVER_CONNECTING,
    CLOUD_EVENT_BUTTON_PRESSED,
    CLOUD_EVENT_DATA_SENT,
    CLOUD_EVENT_CLOUD_CONFIG_RECEIVED
};

/** @brief cloud module event. */
struct cloud_module_event {
	/** app module application event header. */
	struct app_event_header header;
	/** cloud module event type. */
	enum cloud_module_event_type type;
};

APP_EVENT_TYPE_DECLARE(cloud_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _CLOUD_MODULE_EVENT_H_ */