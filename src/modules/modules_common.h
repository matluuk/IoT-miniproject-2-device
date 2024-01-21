/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef _MODULES_COMMON_H_
#define _MODULES_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Macro that checks if an event is of a certain type.
 *
 * @param _ptr Name of module message struct variable.
 * @param _mod Name of module that the event corresponds to.
 * @param _evt Name of the event.
 *
 * @return true if the event matches the event checked for, otherwise false.
 */
#define IS_EVENT(_ptr, _mod, _evt) \
		is_ ## _mod ## _module_event(&_ptr->module._mod.header) &&		\
		_ptr->module._mod.type == _evt

/** @brief Macro used to submit an event.
 *
 * @param _mod Name of module that the event corresponds to.
 * @param _type Name of the type of event.
 */
#define SEND_EVENT(_mod, _type)								\
	struct _mod ## _module_event *event = new_ ## _mod ## _module_event();		\
	__ASSERT(event, "Not enough heap left to allocate event");			\
	event->type = _type;								\
	APP_EVENT_SUBMIT(event)

#ifdef __cplusplus
}
#endif
#endif