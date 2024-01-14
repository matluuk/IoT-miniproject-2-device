#ifndef _APP_MODULE_EVENT_H_
#define _APP_MODULE_EVENT_H_

/**
 * @brief App module event
 * @defgroup app_module_event App module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @brief App event types. */
enum app_module_event_type {
    APP_EVENT_START,
    APP_EVENT_LTE_CONNECT,
    APP_EVENT_LTE_DISCONNECT,
    APP_EVENT_LOCATION_GET,
    APP_EVENT_BATTERY_GET,
    APP_EVENT_CONFIG_GET,
    APP_EVENT_START_MOVEMENT
};

/** @brief App module event. */
struct app_module_event {
	/** App module application event header. */
	struct app_event_header header;
	/** App module event type. */
	enum app_module_event_type type;
};

APP_EVENT_TYPE_DECLARE(app_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _APP_MODULE_EVENT_H_ */