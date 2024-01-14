#ifndef _MODEM_MODULE_EVENT_H_
#define _MODEM_MODULE_EVENT_H_

/**
 * @brief App module event
 * @defgroup modem_module_event App module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @brief App event types. */
enum modem_module_event_type {
    MODEM_EVENT_INTIALIZED,
    MODEM_EVENT_LTE_CONNECTED,
    MODEM_EVENT_LTE_DISCONNECTED,
    MODEM_EVENT_LTE_CONNECTING
};

/** @brief App module event. */
struct modem_module_event {
	/** App module application event header. */
	struct app_event_header header;
	/** App module event type. */
	enum modem_module_event_type type;
};

APP_EVENT_TYPE_DECLARE(modem_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _MODEM_MODULE_EVENT_H_ */