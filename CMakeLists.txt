#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cellular_fundamentals)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
# NORDIC SDK APP END

target_include_directories(app PRIVATE src)
target_sources_ifdef(CONFIG_CJSON app PRIVATE src/cJSON.c)

target_sources(app PRIVATE
		src/events/app_module_event.c
)

# Application directories
add_subdirectory(src/modules)
add_subdirectory(src/events)