# IoT-miniproject-2-device

This is a repository for a for Mini project 2 for Internet of Things - Universioty of Oulu course. More information about the project can be found form the projects main repository: [here](https://github.com/matluuk/IoT-miniproject-2)

The repository implements a location tracker using Nordic Semiconductor nRF9160 SoC. Development board used is [Thingy91](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91).

## Features of the applicaiton
- **Location using GNSS or Cellular.** 

    The location tracker uses GNSS and Cellular based location and sends the location data to CoAp server. The application uses A_GNSS and P_GPS to reduce the location search fix time and improve power efficiency.

- **Runtime change of device config**

    The application fetches the new device config every time the location data is published to the cloud. The device config can therefore be updated during runtime, by changing the confign on CoAp server.

    - **Device config**

        The device config has following fields
        - **active_mode** - 1 for **active mode** and 0 for **passive mode**
        - **location_timeout**  - Change the GNSS search timeout **Currently is not working.**
        - **active_wait_timeout** - time between location searches on active mode.
        - **passive_wait_timeout** - time between location searches on passive mode.

- **Power efficiency**
    - **PSM**

        The application uses LTE PSM Power Saving Mode to save the power needed for the device. PSM is apower saving mode, where the device go into a deep sleep for a longer period of time, but stays reqistered and attached to the LTE network. This reduces the power needed, when the device wakes up and sends uplink data to the CoAp server.

    - **Location search interval**

        The time between location searchs is fully configurable in the device config.

- **Two device modes**

    Currently both modes are functionlly same. They just use different location interval value from device config.

    - **Active mode** 

        Active mode is for continuous/short period location data. For example location search every 5 minutes.

    - **Passive mode** 

        Passive mode is for long location search interval. For example every 12h. Original idea was also to trigger location search, by detecting device movement during passive mode. However this functionality is not implemented.

# inspiration for the project

Nordic semiconductor's course is used as a base for the project: [link](https://academy.nordicsemi.com/courses/cellular-iot-fundamentals/)
The nordic semiconductor example application [asset_tracker_v2](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/applications/asset_tracker_v2/README.html) is used as a inspiration especially for the application module design and event handling, as I didn't have much experience in these parts of the embedded software development.

# Requirements

## Hardware
The application is developed for [nRF9160](https://www.nordicsemi.com/Products/nRF9160) SiP. Specifically for [Thingy91](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91) Multi-sensor cellular IoT prototyping platform. However [nRF9160 DK](https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk) would also work, but small code changes/additions might be needed.

# Application structure

Application is made of modules. There are currently five modules:

main
led_module
location_module
cloud_module
modem_module

Each module has it's own task. 

Communication between modules is handled by events. Each module has it's own events to send and other modules subscribe the necessary events. Also data, such as location data and application config is transmitted between modules using events.

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

List of all modem module events

- MODEM_EVENT_INTIALIZED
- MODEM_EVENT_LTE_CONNECTED
- MODEM_EVENT_LTE_DISCONNECTED
- MODEM_EVENT_LTE_CONNECTING

# building, flashing and development environment

The quide to set up a development environment and build for nRF9160 Thingy91 is [here](https://academy.nordicsemi.com/courses/nrf-connect-sdk-fundamentals/lessons/lesson-1-nrf-connect-sdk-introduction/topic/exercise-1-1/)

Instructions of how to flash the application to the Thingy:91 can be found [here](https://academy.nordicsemi.com/flash-instructions-for-the-thingy91/).

# Future features/fixes to be developed

## Location module
- Only time, latitude, longitude, altitude and accuracy is sent to cloud - Add following information to location data: location_method, search_time, satellites_tracked, speed and heading.
- Implement location timeout. One attempt is commented, as it didn't work.

## Sensors module
    - implement sensors module
    - Features considered to sensors module
        - Battery level measurement
        - Movement detection
        - temperature measurement

## Cloud module
- Only the most recent location data is sent to the cloud - Add queue for location data
- Implement better way to get device config from cloud. Currently It is fetched every time data is sent.

## Led module
- Implement led sate: LED_STATE_ERROR_SYSTEM_FAULT
- Implement led sate: LED_STATE_CLOUD_SENDING_DATA
- Add queue for led states - This would allow predefined time for one led state

## Other
- Move structs to codec.h
- Take EDRx to use instead of PSM, if active wait time is smaller than a treshold value for example 10 min
- Command interface between cloud and device for commands such:
    - reset device
    - turn leds on/off

## Testing
- There are currently no tests for the applicaiton. All testing is done manually.