/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <caf/led_effect.h>
// #include "events/led_state_event.h"

/* This configuration file is included only once from led_state module and holds
 * information about LED effects associated with Asset Tracker v2 states.
 */

/* This structure enforces the header file to be included only once in the build.
 * Violating this requirement triggers a multiple definition error at link time.
 */
const struct {} led_state_def_include_once;

enum led_id {
	LED_ID_1,

	LED_ID_COUNT,

	LED_ID_CONNECTING = LED_ID_1,
	LED_ID_CLOUD_CONNECTING = LED_ID_1,
	LED_ID_SEARCHING = LED_ID_1,
	LED_ID_SENDING_CLOUD_DATA = LED_ID_1,
	LED_ID_PASSIVE_MODE = LED_ID_1,
	LED_ID_ACTIVE_MODE = LED_ID_1,
};

enum led_effect_id {
	LED_STATE_LTE_CONNECTING,
	LED_STATE_LOCATION_SEARCHING,
	LED_STATE_CLOUD_SENDING_DATA,
	LED_STATE_CLOUD_CONNECTING,
	LED_STATE_ACTIVE_MODE,
	LED_STATE_PASSIVE_MODE,
	LED_STATE_ERROR_SYSTEM_FAULT,
	LED_STATE_TURN_OFF,


	LED_EFFECT_ID_COUNT
};

#define LED_PERIOD_NORMAL	350
#define LED_PERIOD_RAPID	100
#define LED_TICKS_DOUBLE	2
#define LED_TICKS_TRIPLE	3

static const struct led_effect led_effect[] = {
	[LED_STATE_LTE_CONNECTING]	= LED_EFFECT_LED_BREATH(LED_PERIOD_NORMAL,
								LED_COLOR(255, 255, 0)),
	[LED_STATE_LOCATION_SEARCHING]	= LED_EFFECT_LED_BREATH(LED_PERIOD_NORMAL,
								LED_COLOR(255, 0, 255)),
	[LED_STATE_CLOUD_SENDING_DATA]	= LED_EFFECT_LED_CLOCK(LED_TICKS_TRIPLE,
								LED_COLOR(0, 255, 255)),
	[LED_STATE_CLOUD_CONNECTING]	= LED_EFFECT_LED_CLOCK(LED_TICKS_TRIPLE,
							       LED_COLOR(0, 255, 0)),
	[LED_STATE_ACTIVE_MODE]		= LED_EFFECT_LED_BREATH(LED_PERIOD_NORMAL,
								LED_COLOR(0, 255, 0)),
	[LED_STATE_PASSIVE_MODE]	= LED_EFFECT_LED_BREATH(LED_PERIOD_NORMAL,
								LED_COLOR(0, 0, 255)),
	[LED_STATE_ERROR_SYSTEM_FAULT]	= LED_EFFECT_LED_ON(LED_COLOR(255, 0, 0)),
	[LED_STATE_TURN_OFF]		= LED_EFFECT_LED_OFF(),
};
