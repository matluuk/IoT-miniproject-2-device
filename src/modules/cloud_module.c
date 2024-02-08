#include <stdio.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>
#include <app_event_manager.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

/* Include the header file for the CoAP library */
#include <zephyr/net/coap.h>

#include "codec.h"

#include <cJSON.h>
#include <date_time.h>

#include "modules/modules_common.h"
#include "events/app_module_event.h"
#include "events/cloud_module_event.h"
#include "events/modem_module_event.h"
#include "events/location_module_event.h"

#define MODULE cloud_module

/* Cloud module super states. */
static enum state_type {
	STATE_LTE_INIT,
	STATE_LTE_DISCONNECTED,
	STATE_LTE_CONNECTED,
	STATE_SHUTDOWN
} state;

/* Cloud module sub states*/
static enum sub_state_type {
	SUB_STATE_SERVER_DISCONNECTED,
	SUB_STATE_SERVER_CONNECTED,
} sub_state;

struct cloud_msg_data {
	union {
		struct app_module_event app;
		struct cloud_module_event cloud;
		struct modem_module_event modem;
		struct location_module_event location;
	} module;
};

struct cloud_pvt {
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

struct cloud_location_data {
	/** PVT data*/
	struct cloud_pvt pvt;
	/** GNSS data timestamp. UNIX milliseconds. */
	int64_t gnss_ts;
};

#define MSG_Q_SIZE 20

K_MSGQ_DEFINE(msgq_cloud, sizeof(struct cloud_msg_data), MSG_Q_SIZE, 4);

/* Local copy of the device configuration. */
static struct app_cfg copy_cfg;

/* Define the macros for the CoAP version and message length */
#define APP_COAP_VERSION 1
#define APP_COAP_MAX_MSG_LEN 1280

/* Declare the buffer coap_buf to receive the response. */
static uint8_t coap_buf[APP_COAP_MAX_MSG_LEN];

/* Define the CoAP message token next_token */
static uint16_t next_token;

static int sock;

K_SEM_DEFINE(socket_sem, 1, 1);  // Initialize a semaphore with an initial count of 1 and a maximum count of 1

static struct sockaddr_storage server;

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

static char *sub_state_to_string(enum sub_state_type sub_state)
{
	switch (sub_state)
	{
	case SUB_STATE_SERVER_CONNECTED:
		return "SUB_STATE_SERVER_CONNECTED";
	case SUB_STATE_SERVER_DISCONNECTED:
		return "SUB_STATE_SERVER_DISCONNECTED";
	default:
		return "Unknown";
	}
}

static char *state_to_string(enum state_type state)
{
	switch (state)
	{
	case STATE_LTE_INIT:
		return "STATE_LTE_INIT";
	case STATE_LTE_DISCONNECTED:
		return "STATE_LTE_DISCONNECTED";
	case STATE_LTE_CONNECTED:
		return "STATE_LTE_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static void set_state(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state_to_string(state));
		return;
	}
	LOG_DBG("State transition: %s -> %s", 
		state_to_string(state),
		state_to_string(new_state));
	state = new_state;
}

static void set_sub_state(enum sub_state_type new_sub_state)
{
	if (new_sub_state == sub_state) {
		LOG_DBG("Sub state: %s", state_to_string(sub_state));
		return;
	}
	LOG_DBG("Sub state transition: %s -> %s", 
		sub_state_to_string(sub_state),
		sub_state_to_string(new_sub_state));
	sub_state = new_sub_state;
}

static bool app_event_handler(const struct app_event_header *aeh){
	bool consume = false, enqueue_msg = false;
	struct cloud_msg_data msg = {0};
	if (is_app_module_event(aeh)){
		struct app_module_event *event = cast_app_module_event(aeh);
		msg.module.app = *event;
		enqueue_msg = true;
	}
	
	if (is_cloud_module_event(aeh)){
		struct cloud_module_event *event = cast_cloud_module_event(aeh);
		msg.module.cloud = *event;
		enqueue_msg = true;
	}

	
	if (is_modem_module_event(aeh)){
		struct modem_module_event *event = cast_modem_module_event(aeh);
		msg.module.modem = *event;
		enqueue_msg = true;
	}

	
	if (is_location_module_event(aeh)){
		struct location_module_event *event = cast_location_module_event(aeh);
		msg.module.location = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg){
		 /* Add the event to the message queue */
        int err = k_msgq_put(&msgq_cloud, &msg, K_NO_WAIT);
        if (err) {
            LOG_ERR("Failed to add event to message queue: %d", err);
            /* Handle the error */
        }
	}

	return consume;
}

static int server_resolve(void)
{
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);

	/* Set the server IP address and port directly */
	inet_pton(AF_INET, CONFIG_COAP_SERVER_IP, &server4->sin_addr);
	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_COAP_SERVER_PORT);

	char ipv4_addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &server4->sin_addr, ipv4_addr, sizeof(ipv4_addr));
	LOG_INF("IPv4 Address set %s\n", ipv4_addr);

	return 0;
}

/**@brief Initialize the CoAP client */
static int client_init(void)
{
	int err;

	k_sem_take(&socket_sem, K_FOREVER); // Take the semaphore

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create CoAP socket: %d.\n", errno);
		return -errno;
	}

	err = connect(sock, (struct sockaddr *)&server,
				  sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connect failed : %d\n", errno);
		return -errno;
	}

	k_sem_give(&socket_sem); // Give the semaphore

	LOG_INF("Successfully connected to server");

	/* Generate a random token after the socket is connected */
	next_token = sys_rand32_get();

	return 0;
}

/**@biref Send CoAP request. */
static int client_send_request(const char *resource_path, uint8_t content_type, const char *payload, uint8_t method, enum coap_msgtype type)
{
	int err;
	struct coap_packet request;

	next_token++;

	/* Initialize the CoAP packet and append the resource path */
	err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
				   APP_COAP_VERSION, type,
				   sizeof(next_token), (uint8_t *)&next_token,
				   method, coap_next_id());
	if (err < 0) {
		LOG_ERR("Failed to create CoAP request, %d\n", err);
		return err;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					(uint8_t *)resource_path,
					strlen(resource_path));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d\n", err);
		return err;
	}

	/* Append the content format */
	err = coap_packet_append_option(&request, COAP_OPTION_CONTENT_FORMAT,
					&content_type,
					sizeof(content_type));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d\n", err);
		return err;
	}

	/* Add the payload to the message */
	if (payload != NULL) {
		err = coap_packet_append_payload_marker(&request);
		if (err < 0) {
			LOG_ERR("Failed to append payload marker, %d\n", err);
			return err;
		}

		err = coap_packet_append_payload(&request, (uint8_t *)payload, strlen(payload));
		if (err < 0) {
			LOG_ERR("Failed to append payload, %d\n", err);
			return err;
		}
	}

	k_sem_take(&socket_sem, K_FOREVER); // Take the semaphore

	err = send(sock, request.data, request.offset, 0);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request, %d\n", errno);
		return -errno;
	}

	k_sem_give(&socket_sem); // Give the semaphore

	LOG_INF("CoAP request sent: Token 0x%04x\n", next_token);

	struct cloud_module_event *cloud_module_event = new_cloud_module_event();
	cloud_module_event->type = CLOUD_EVENT_DATA_SENT;
	APP_EVENT_SUBMIT(cloud_module_event);

	return 0;
}

static int client_send_location_data(struct cloud_location_data *location_data)
{	
	int err;
	err = date_time_uptime_to_unix_time_ms(&location_data->gnss_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	time_t rawtime = (time_t)(location_data->gnss_ts / 1000); // Convert milliseconds to seconds
    struct tm *tm_info = localtime(&rawtime);

    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

	cJSON *root = cJSON_CreateObject();
	if (root == NULL) {
		printf("Error: cJSON_CreateObject failed\n");
		return -1;
	}

	if (!cJSON_AddStringToObject(root, "time", time_str)) {
		LOG_ERR("Error: cJSON_AddStringToObject failed for time\n");
		cJSON_Delete(root);
		return -1;
	}

	if (!cJSON_AddNumberToObject(root, "latitude", location_data->pvt.latitude)) {
		LOG_ERR("Error: cJSON_AddNumberToObject failed for latitude\n");
		cJSON_Delete(root);
		return -1;
	}

	if (!cJSON_AddNumberToObject(root, "longitude", location_data->pvt.longitude)) {
		LOG_ERR("Error: cJSON_AddNumberToObject failed for longitude\n");
		cJSON_Delete(root);
		return -1;
	}

	if (!cJSON_AddNumberToObject(root, "altitude", location_data->pvt.altitude)) {
		LOG_ERR("Error: cJSON_AddNumberToObject failed for altitude\n");
		cJSON_Delete(root);
		return -1;
	}

	if (!cJSON_AddNumberToObject(root, "accuracy", location_data->pvt.accuracy)) {
		LOG_ERR("Error: cJSON_AddNumberToObject failed for accuracy\n");
		cJSON_Delete(root);
		return -1;
	}
	char *payload = cJSON_Print(root);
	if (payload == NULL) {
		LOG_ERR("Error: cJSON_Print failed\n");
		cJSON_Delete(root);
		return -1;
	}

	LOG_INF("Sending: %s", payload);
	client_send_request(CONFIG_COAP_DATA_RESOURCE, COAP_CONTENT_FORMAT_APP_JSON, payload, COAP_METHOD_POST, COAP_TYPE_NON_CON);

	cJSON_Delete(root);
	free(payload);

	return 0;
}

static int client_get_device_config()
{
	client_send_request(CONFIG_COAP_DEVICE_CONFIG_RESOURCE, COAP_CONTENT_FORMAT_TEXT_PLAIN, NULL, COAP_METHOD_GET, COAP_TYPE_CON);

	return 0;
}

static int handle_device_config_responce(char *device_config) {
    cJSON *root = cJSON_Parse(device_config);
    if (root == NULL) {
        printf("Error: cJSON_Parse failed\n");
        return -1;
    }

    cJSON *device_id = cJSON_GetObjectItem(root, "device_id");
    cJSON *active_mode = cJSON_GetObjectItem(root, "active_mode");
    cJSON *location_timeout = cJSON_GetObjectItem(root, "location_timeout");
    cJSON *active_wait_timeout = cJSON_GetObjectItem(root, "active_wait_timeout");
    cJSON *passive_wait_timeout = cJSON_GetObjectItem(root, "passive_wait_timeout");

	struct app_cfg new_cfg;


    if (device_id != NULL && cJSON_IsNumber(device_id)) {
		new_cfg.device_id = device_id->valueint;
	}

	if (active_mode != NULL && cJSON_IsNumber(active_mode)) {
		new_cfg.active_mode = active_mode->valueint;
	}

	if (location_timeout != NULL && cJSON_IsNumber(location_timeout)) {
		new_cfg.location_timeout = location_timeout->valueint;
	}

	if (active_wait_timeout != NULL && cJSON_IsNumber(active_wait_timeout)) {
		new_cfg.active_wait_timeout = active_wait_timeout->valueint;
	}

	if (passive_wait_timeout != NULL && cJSON_IsNumber(passive_wait_timeout)) {
		new_cfg.passive_wait_timeout = passive_wait_timeout->valueint;
	}
	
	struct cloud_module_event *cloud_module_event = new_cloud_module_event();
	cloud_module_event->type = CLOUD_EVENT_CLOUD_CONFIG_RECEIVED;
	cloud_module_event->cloud_cfg = new_cfg;
	APP_EVENT_SUBMIT(cloud_module_event);

    cJSON_Delete(root);
	return 0;
}

/**@brief Handles responses from the remote CoAP server. */
static int client_handle_response(uint8_t *buf, int received)
{
	struct coap_packet reply;
	uint8_t token[8];
	uint16_t token_len;
	const uint8_t *payload;
	uint16_t payload_len;
	uint8_t temp_buf[128];
	/* Parse the received CoAP packet */
	int err = coap_packet_parse(&reply, buf, received, NULL, 0);
	if (err < 0) {
		LOG_ERR("Malformed response received: %d\n", err);
		return err;
	}

	/* Confirm the token in the response matches the token sent */
	token_len = coap_header_get_token(&reply, token);
	if ((token_len != sizeof(next_token)) ||
	    (memcmp(&next_token, token, sizeof(next_token)) != 0)) {
		LOG_ERR("Invalid token received: 0x%02x%02x\n",
		       token[1], token[0]);
		return 0;
	}

	/* Retrieve the payload and confirm it's nonzero */
	payload = coap_packet_get_payload(&reply, &payload_len);

	if (payload_len > 0) {
		snprintf(temp_buf, MIN(payload_len + 1, sizeof(temp_buf)), "%s", payload);

		err = handle_device_config_responce(temp_buf);
		if (err < 0){
			LOG_ERR("Failed to handle device config responce");
			return err;
		}
	} else {
		strcpy(temp_buf, "EMPTY");
	}

	/* Log the header code, token and payload of the response */
	LOG_INF("CoAP response: Code 0x%x, Token 0x%02x%02x, Payload: %s\n",
	       coap_header_get_code(&reply), token[1], token[0], (char *)temp_buf);

	return 0;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	/*Send a GET request or PUT request upon button triggers */
	if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK) {
		struct cloud_module_event *cloud_module_event = new_cloud_module_event();
		cloud_module_event->type = CLOUD_EVENT_BUTTON_PRESSED;
		APP_EVENT_SUBMIT(cloud_module_event);
	}
}

static void connect_cloud() {
	if (server_resolve() != 0) {
		LOG_INF("Failed to resolve server name");
	}

	if (client_init() != 0) {
		LOG_INF("Failed to initialize client");
	}

	struct cloud_module_event *cloud_module_event = new_cloud_module_event();
	cloud_module_event->type = CLOUD_EVENT_SERVER_CONNECTED;
	APP_EVENT_SUBMIT(cloud_module_event);
}

static void on_state_lte_init(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVENT_START)){
		set_state(STATE_LTE_DISCONNECTED);
		set_sub_state(SUB_STATE_SERVER_DISCONNECTED);
	}
}

static void on_state_lte_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_CONNECTED)){
		set_state(STATE_LTE_CONNECTED);
		struct cloud_module_event *cloud_module_event = new_cloud_module_event();
		cloud_module_event->type = CLOUD_EVENT_SERVER_CONNECTING;
		APP_EVENT_SUBMIT(cloud_module_event);
		connect_cloud();
	}
}

static void on_state_lte_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_DISCONNECTED)){
		set_state(STATE_LTE_DISCONNECTED);
		set_sub_state(SUB_STATE_SERVER_DISCONNECTED);
	}
}

static void on_sub_state_server_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVENT_SERVER_CONNECTED)){
		set_sub_state(SUB_STATE_SERVER_CONNECTED);
		client_get_device_config();
	}
}

static void on_sub_state_server_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVENT_SERVER_DISCONNECTED)){
		set_sub_state(SUB_STATE_SERVER_DISCONNECTED);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVENT_BUTTON_PRESSED)){
		struct app_module_event *app_module_event = new_app_module_event();
		app_module_event->type = APP_EVENT_LOCATION_GET;
		APP_EVENT_SUBMIT(app_module_event);
	}

	if (IS_EVENT(msg, location, LOCATION_EVENT_GNSS_DATA_READY)){
		struct cloud_location_data new_location_data = {
			.gnss_ts = msg->module.location.location.timestamp
		};

		new_location_data.pvt.longitude = msg->module.location.location.pvt.longitude;
		new_location_data.pvt.latitude = msg->module.location.location.pvt.latitude;
		new_location_data.pvt.altitude = msg->module.location.location.pvt.altitude;
		new_location_data.pvt.accuracy = msg->module.location.location.pvt.accuracy;
		new_location_data.pvt.speed = msg->module.location.location.pvt.speed;
		new_location_data.pvt.heading = msg->module.location.location.pvt.heading;
		
		LOG_DBG("New_location_data:");
		LOG_DBG("  latitude: %.06f", new_location_data.pvt.latitude);
		LOG_DBG("  longitude: %.06f", new_location_data.pvt.longitude);
		LOG_DBG("  accuracy: %.01f m", new_location_data.pvt.accuracy);
		LOG_DBG("  altitude: %.01f m", new_location_data.pvt.altitude);
		LOG_DBG("  speed: %.01f m", new_location_data.pvt.speed);
		LOG_DBG("  heading: %.01f deg", new_location_data.pvt.heading);
		
		client_send_location_data(&new_location_data);
		client_get_device_config();
	}
	
}

static void on_all_states(struct cloud_msg_data *msg){
	if ((IS_EVENT(msg, app, APP_EVENT_START)) || 
		(IS_EVENT(msg, app, APP_EVENT_CONFIG_UPDATE))){
		copy_cfg = msg->module.app.app_cfg;
	}
}


int cloud_thread_fn(void)
{
	int err;
    struct cloud_msg_data msg = {0};

	LOG_INF("started!");

	k_sleep(K_SECONDS(3));

	LOG_INF("Cloud module started");

	if (dk_buttons_init(button_handler) != 0) {
		LOG_ERR("Failed to initialize the buttons library");
	}

	while (1) {
        err = k_msgq_get(&msgq_cloud, &msg, K_FOREVER);
		if (err) {
            LOG_ERR("Failed to get event from message queue: %d", err);
            /* Handle the error */
        } else {
			switch (state)
			{
			case STATE_LTE_INIT:
				on_state_lte_init(&msg);
				break;
			case STATE_LTE_DISCONNECTED:
				on_state_lte_disconnected(&msg);
				break;
			case STATE_LTE_CONNECTED:
				switch (sub_state)
				{
				case SUB_STATE_SERVER_DISCONNECTED:
					on_sub_state_server_disconnected(&msg);
					break;
				case SUB_STATE_SERVER_CONNECTED:
					on_sub_state_server_connected(&msg);
					break;
				default:
					break;
				}
				on_state_lte_connected(&msg);
				break;
			case STATE_SHUTDOWN:
				break;
			
			default:
				LOG_ERR("Unknown state");
				break;
			}
			on_all_states(&msg);
		}
	}

	k_sem_take(&socket_sem, K_FOREVER); // Take the semaphore

	(void)close(sock);

	k_sem_give(&socket_sem); // Give the semaphore

	return 0;
}

void coap_response_thread_fs(void *arg1, void *arg2, void *arg3){

	int received;
	int err;

    while (1) 
	{
		if (state == STATE_LTE_CONNECTED && sub_state == SUB_STATE_SERVER_CONNECTED) {

			k_sem_take(&socket_sem, K_FOREVER); // Take the semaphore
			
			received = recv(sock, coap_buf, sizeof(coap_buf), ZSOCK_MSG_DONTWAIT);

			k_sem_give(&socket_sem); // Give the semaphore

			if (received < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* No data available, yield the thread to allow other threads to run */
					k_yield();
					continue;
				} else {
					LOG_ERR("Socket error: %d, exit\n", errno);
					break;
				}
			} else if (received == 0) {
				LOG_INF("Empty datagram\n");
				continue;
			}

			/* Parse the received CoAP packet */
			err = client_handle_response(coap_buf, received);
			if (err < 0) {
				LOG_ERR("Handle response error: %d, exit\n", errno);
			}
		}
		else {
			k_yield();
		}
    }
}

K_THREAD_DEFINE(coap_thread_id, 2048,
		coap_response_thread_fs, NULL, NULL, NULL, 
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

K_THREAD_DEFINE(cloud_module_thread, 2048,
		cloud_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
	
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_module_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_module_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_module_event);
APP_EVENT_SUBSCRIBE(MODULE, location_module_event);