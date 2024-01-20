#ifndef CODEC_H__
#define CODEC_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct app_cfg{
	/**Device id for identifying the device*/
	int device_id;
	/**Device mode: Active or Passive*/
	bool active_mode;
	/**Location search timeout*/
	int location_timeout;
	/**Delay between location search in active mode*/
	int active_wait_timeout;
	/**Delay between location search in passive mode*/
	int passive_wait_timeout;
};

#ifdef __cplusplus
}
#endif
#endif