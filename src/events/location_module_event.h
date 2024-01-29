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
    LOCATION_EVENT_ERROR,
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

/** @brief Date and time (UTC). */
struct location_module_datetime {
	/** True if date and time are valid, false if not. */
	bool valid;
	/** 4-digit representation (Gregorian calendar). */
	uint16_t year;
	/** 1...12 */
	uint8_t month;
	/** 1...31 */
	uint8_t day;
	/** 0...23 */
	uint8_t hour;
	/** 0...59 */
	uint8_t minute;
	/** 0...59 */
	uint8_t second;
	/** 0...999 */
	uint16_t ms;
};

/** Location method. */
enum location_data_method {
	/** LTE cellular positioning. */
	LOCATION_DATA_METHOD_CELLULAR = 1,
	/** Global Navigation Satellite System (GNSS). */
	LOCATION_DATA_METHOD_GNSS,
	/** Wi-Fi positioning. */
	LOCATION_DATA_METHOD_WIFI,
};

/** LOCATION_DATA data. */
struct location_module_data{
	/** PVT data*/
    struct location_module_pvt pvt;

	/** Location data mode*/
	enum location_data_method method;

	/** Number of satellites tracked. */
	uint8_t satellites_tracked;

	/** Time when the search was initiated until fix or timeout occurred. */
	uint32_t search_time;

	/** Uptime when location was sampled. */
	int64_t timestamp;

	/**  Date and time (UTC). */
	struct location_module_datetime datetime;
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