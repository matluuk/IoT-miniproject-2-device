# IoT-miniproject-2

This is a repository for a for Mini project 2 for Internet of Things - Universioty of Oulu course.

# Project idea

The idea of this project is to develop a simple location tracker for a bike. 

The device should be able to work multiple days continuosly.

The Location data would be searched periodically, for example one time in a day. Additionally a accelerometer would be used to detect bike movement and trigger location search.

Two device modes would be implemented. Active mode for continuous location search, and Passive mode for movement detection and 

A website would be made for watching the bike location ands changing the tracker settings such as device mode and time between periodic location searchs.

# inspiration for the project

This projects is built on top of Nordic semiconductor's course: [link](https://academy.nordicsemi.com/courses/cellular-iot-fundamentals/)
The nordic semiconductor example application [asset_tracker_v2](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/applications/asset_tracker_v2/README.html) is used as a inspiration. I took inspiration especially for the application module design and event handling, as I didn't have much experience in these parts of the embedded software development.

# Requirements

## Hardware
The applicarion is developed for [nRF9160](https://www.nordicsemi.com/Products/nRF9160) SiP. Specifically for [Thingy91](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91) Multi-sensor cellular IoT prototyping platform. However [nRF9160 DK](https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk) would also work with small code changes.


# building, flashing and development environment

The quide to set up a development environment and build for nRF9160 Thingy91 is [here](https://academy.nordicsemi.com/courses/nrf-connect-sdk-fundamentals/lessons/lesson-1-nrf-connect-sdk-introduction/topic/exercise-1-1/)

Instructions of how to flash the application to the Thingy:91 can be found [here](https://academy.nordicsemi.com/flash-instructions-for-the-thingy91/).

# Application structure

Application is made of modules. There are currently five modules:

main
led_module
location_module
cloud_module
modem_module

Each module has it's own task. 

Communication between modules is handled by events. Each module has it's own events to send and other modules subscribe the necessary events. Also data, such location data and application config is sent using events.

## main

Main module is module, where the program starts. The main modules task is to bring all modules together and handle device modes: active and passive. Main module requests location data from locaiton module using atimer. It also handles application config received by cloud module and changes the device modes according to the config.

### app module event

App module events are events that are sent by main module.

List of app module events:

- APP_EVENT_START
- APP_EVENT_LTE_CONNECT
- APP_EVENT_LTE_DISCONNECT
- APP_EVENT_LOCATION_GET
- APP_EVENT_BATTERY_GET
- APP_EVENT_CONFIG_GET
- APP_EVENT_CONFIG_UPDATE
- APP_EVENT_START_MOVEMENT

## led_module

Led module handles the on board status led. All led states are explained below.

### led_module_events

Led module doesn't have any events.

### led states

|Led state name|Application state|Led style|Led color|Is used in application|
|---|---|---|---|---|
|LED_STATE_LTE_CONNECTING|During lte search|BREATH|Yellow|Yes|
|LED_STATE_LOCATION_SEARCHING|During location search|BREATH|Purple|Yes|
|LED_STATE_CLOUD_SENDING_DATA|Displayed shortly, when could data is sent.|one blink|light blue|Currently not implemented|
|LED_STATE_CLOUD_CONNECTING|Displayed, when CoAp server is connecting|three blinks|green|Yes|
|LED_STATE_ACTIVE_MODE|Displayed, when application is running in active mode.|BREATH|green|Yes|
|LED_STATE_PASSIVE_MODE|Displayed, when application is running in passive mode.|BREATH|Blue|Yes|
|LED_STATE_ERROR_SYSTEM_FAULT|Error in application|Continuous|Red|Currently not implemented|

## location_module

Location module handles device locaiton searchs. Location module uses a nordic semiconductors location library for retreiving location using positioning methods GNSS and Cellular positioning. GNSS satellite positioning includes Assisted GNSS (A-GNSS) and Predicted GPS (P-GPS) data. The data needed for Cellular positioning, A_GNSS and P-GPS are retreived from nordic nRF cloud.

! Notice that currently the device configs location timeout is not implemented. 

### location module events

List of all location module events

- LOCATION_EVENT_GNSS_DATA_READY
- LOCATION_EVENT_TIMEOUT
- LOCATION_EVENT_ERROR
- LOCATION_EVENT_ACTIVE
- LOCATION_EVENT_INACTIVE

## cloud_module

Could module handles the connection to the cloud. Module implements a CoAp connection to a CoAp server, that has two resources: "data" and "device_config". Location data is sent to the "data" resource using CoAp PUT method and device configuration is fetched from "device_config" using CoAp GET method. Device config is fetched every time location data is sent to cloud.

Could module listens to CoAp messages asynchronously, when server is connected to cloud. The message responces are witing on its own thread. The implemenation is poor as CoAp packets are waited even, if application doesn't excpect a message.

### Cloud module events
List of all cloud module events

- CLOUD_EVENT_INTIALIZED
- CLOUD_EVENT_SERVER_DISCONNECTED
- CLOUD_EVENT_SERVER_CONNECTING
- CLOUD_EVENT_SERVER_CONNECTED
- CLOUD_EVENT_BUTTON_PRESSED
- CLOUD_EVENT_DATA_SENT
- CLOUD_EVENT_CLOUD_CONFIG_RECEIVED

## modem_module

Modem modules task is to handle teh modem and lte connection. The module intializes the modem and AT library as well as handles lte connection. 

### modem module events

List of alöl modem module events

- MODEM_EVENT_INTIALIZED
- MODEM_EVENT_LTE_CONNECTED
- MODEM_EVENT_LTE_DISCONNECTED
- MODEM_EVENT_LTE_CONNECTING

# TODO:
Location
- Only time, latitude, longitude, altitude and accuracy is sent to cloud - Add method, search_time, satellites_tracked, location method, speed and heading.
- Implement location timeout. One attempt is in comments as it didn't work.

## Other
- Move structs to codec.h
- Take EDRx to use instead of PSM, if active wait time is smaller than a treshold value for example 10 min

## Sensors module
    - implement sensors module
    - Battery level
    - Movement detection
    - temperature

## Cloud
- Only the most recent location data is sent to the cloud - Add queue for location data
- Implement better way to get device config from cloud. Currently It is fetched every time data is sent.

## LEDs
- Implement led sate: LED_STATE_ERROR_SYSTEM_FAULT
- Implement led sate: LED_STATE_CLOUD_SENDING_DATA
- Add queue for led states so that time of a led state can be specified.

## New features
- Command interface between cloud and device for commands such:
    - reset device
    - start location search
    - turn leds on/off

## Testing

- Verify that location timeout is set properly