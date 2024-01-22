# IoT-miniproject-2
This is a repository of a course work Mini project 2 for course  Internet of Things - Universioty of Oulu

# inspiration
This projects is built on top of Nordic semiconductor's course: [link](https://academy.nordicsemi.com/courses/cellular-iot-fundamentals/)
The nordic semiconductor example application asset_tracker_v2 is used asa inspiration especially for application design and event handling.

# TODO:
Location
- Only time, latitude, longitude, altitude and accuracy is sent to cloud - Add method, search_time, satellites_tracked, location method, speed and heading.

Other
- Move structs to codec.h
- Fix PSM. Now it is rejected
- Update PSM and eDRX parameters when device config is received.
- Add sensors module
    - Battery level
    - Movement detection
    - Other sensors?

Cloud
- Only the most recent location data is sent to the cloud - Add queue for location data
- Implement better way to get device config from cloud. Currently It is fetched every time data is sent.

LEDs
- Add led module to control led color and blinking

New features
- Command interface between cloud and device for commands such:
    - reset device
    - start location search
    - turn leds on/off

Testing

- Verify that location timeout is set properly