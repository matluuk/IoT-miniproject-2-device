
target_include_directories(app PRIVATE .)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_module.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cloud_module.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/location_module.c)