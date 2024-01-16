#ifndef _LOCATION_MODULE_EVENT_H_
#define _LOCATION_MODULE_EVENT_H_

/**
 * @brief Location module event
 * @defgroup location_module_event Location module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Location event types. */
enum location_module_event_type {
    LOCATION_EVENT_GNSS_DATA_READY,
    LOCATION_EVENT_TIMEOUT,
    LOCATION_EVENT_ACTIVE,
    LOCATION_EVENT_INACTIVE
};

/** @brief Position, velocity and time (PVT) data. */
struct location_module_pvt {
	/** Longitude in degrees. */
	double longitude;

	/** Latitude in degrees. */
	double latitude;

	/** Altitude above WGS-84 ellipsoid in meters. */
	float altitude;

	/** Position accuracy (2D 1-sigma) in meters. */
	float accuracy;

	/** Horizontal speed in m/s. */
	float speed;

	/** Heading of user movement in degrees. */
	float heading;
};

/** Location data. */
struct location_module_data{
	/** PVT data*/
    struct location_module_pvt pvt;

	/** Number of satellites tracked. */
	uint8_t satellites_tracked;

	/** Time when the search was initiated until fix or timeout occurred. */
	uint32_t search_time;

	/** Uptime when location was sampled. */
	int64_t timestamp;
};

/** @brief Location module event. */
struct location_module_event {
    /** Location module application event header. */
    struct app_event_header header;
    /** Location module event type. */
    enum location_module_event_type type;
    /** Location data. */
    struct location_module_data location;
};

APP_EVENT_TYPE_DECLARE(location_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _LOCATION_MODULE_EVENT_H_ */