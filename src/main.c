#include <stdio.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

#include "cJSON.h"

/* Include the header file for the CoAP library */
#include <zephyr/net/coap.h>

#define MESSAGE_TO_SEND "Hello from thingy91!"

/* Application module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

/* Application sub states. The application can be in either active or passive
 * mode.
 *
 * Active mode: Sensor GNSS position is acquired at a configured
 *		interval and sent to cloud.
 *
 * Passive mode: Sensor GNSS position is acquired when movement is
 *		 detected, or after the configured movement timeout occurs.
 */
static enum sub_state_type {
	SUB_STATE_ACTIVE_MODE,
	SUB_STATE_PASSIVE_MODE,
} sub_state;

struct {
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
} app_cfg;

static struct nrf_modem_gnss_pvt_data_frame pvt_data;

static int64_t gnss_start_time;
static bool first_fix = false;

static bool button_pressed = false;

/* STEP 4.2 - Define the macros for the CoAP version and message length */
#define APP_COAP_VERSION 1
#define APP_COAP_MAX_MSG_LEN 1280

/* STEP 5 - Declare the buffer coap_buf to receive the response. */
static uint8_t coap_buf[APP_COAP_MAX_MSG_LEN];

/* STEP 6.1 - Define the CoAP message token next_token */
static uint16_t next_token;

static int sock;
static struct sockaddr_storage server;

static K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static char *sub_state_to_string(enum sub_state_type sub_state)
{
	switch (sub_state)
	{
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
	default:
		return "Unknown";
	}
}

static char *state_to_string(enum state_type state)
{
	switch (state)
	{
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
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

	LOG_INF("Successfully connected to server");

	/* STEP 6.2 - Generate a random token after the socket is connected */
	next_token = sys_rand32_get();

	return 0;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
			(evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}
		LOG_INF("Network registration status: %s",
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
				"Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s",
				evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
				"Connected" : "Idle");
		break;
	/* STEP 9.1 - On event PSM update, print PSM paramters and check if was enabled */
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_INF("PSM parameter update: Periodic TAU: %d s, Active time: %d s",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		if (evt->psm_cfg.active_time == -1){
			LOG_ERR("Network rejected PSM parameters. Failed to enable PSM");
		}
		break;
	/* STEP 9.2 - On event eDRX update, print eDRX paramters */
	case LTE_LC_EVT_EDRX_UPDATE:
		LOG_INF("eDRX parameter update: eDRX: %f, PTW: %f",
			evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		break;
	default:
		break;
	}
}

static int modem_configure(void)
{
	int err;

	LOG_INF("Initializing modem library");

	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Failed to initialize the modem library, error: %d", err);
		return err;
	}

	/* STEP 8 - Request PSM and eDRX from the network */
	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("lte_lc_psm_req, error: %d", err);
	}

	err = lte_lc_edrx_req(true);
	if (err) {
		LOG_ERR("lte_lc_edrx_req, error: %d", err);
	}

	LOG_INF("Connecting to LTE network");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Modem could not be configured, error: %d", err);
		return err;
	}

	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");
	dk_set_led_on(DK_LED2);

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

	err = send(sock, request.data, request.offset, 0);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request, %d\n", errno);
		return -errno;
	}

	LOG_INF("CoAP request sent: Token 0x%04x\n", next_token);

	return 0;
}

static int client_send_post_request()
{
	time_t t = time(NULL);
			struct tm *tm_info = localtime(&t);

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

			if (!cJSON_AddNumberToObject(root, "latitude", 67.111)) {
				LOG_ERR("Error: cJSON_AddNumberToObject failed for latitude\n");
				cJSON_Delete(root);
				return -1;
			}

			if (!cJSON_AddNumberToObject(root, "longitude", 27.111)) {
				LOG_ERR("Error: cJSON_AddNumberToObject failed for longitude\n");
				cJSON_Delete(root);
				return -1;
			}
			LOG_INF("Sending: %s", cJSON_Print(root));
			char *payload = cJSON_Print(root);
			if (payload == NULL) {
				LOG_ERR("Error: cJSON_Print failed\n");
				cJSON_Delete(root);
				return -1;
			}

			client_send_request(CONFIG_COAP_TX_RESOURCE, COAP_CONTENT_FORMAT_APP_JSON, payload, COAP_METHOD_POST, COAP_TYPE_NON_CON);

			cJSON_Delete(root);
			free(payload);

			return 0;
}

static int client_get_device_config()
{
	client_send_request("device_config", COAP_CONTENT_FORMAT_TEXT_PLAIN, NULL, COAP_METHOD_GET, COAP_TYPE_CON);

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

    if (device_id != NULL && cJSON_IsNumber(device_id)) {
		app_cfg.device_id = device_id->valueint;
		LOG_INF("Device ID: %d", app_cfg.device_id);
	}

	if (active_mode != NULL && cJSON_IsNumber(active_mode)) {
		app_cfg.active_mode = active_mode->valueint;
		LOG_INF("Active Mode: %d", app_cfg.active_mode);
	}

	if (location_timeout != NULL && cJSON_IsNumber(location_timeout)) {
		app_cfg.location_timeout = location_timeout->valueint;
		LOG_INF("Location Timeout: %d", app_cfg.location_timeout);
	}

	if (active_wait_timeout != NULL && cJSON_IsNumber(active_wait_timeout)) {
		app_cfg.active_wait_timeout = active_wait_timeout->valueint;
		LOG_INF("Active Wait Timeout: %d", app_cfg.active_wait_timeout);
	}

	if (passive_wait_timeout != NULL && cJSON_IsNumber(passive_wait_timeout)) {
		app_cfg.passive_wait_timeout = passive_wait_timeout->valueint;
		LOG_INF("Passive Wait Timeout: %d", app_cfg.passive_wait_timeout);
	}

	LOG_INF("Device config updated successfully!");

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
	/* STEP 9.1 - Parse the received CoAP packet */
	int err = coap_packet_parse(&reply, buf, received, NULL, 0);
	if (err < 0) {
		LOG_ERR("Malformed response received: %d\n", err);
		return err;
	}

	/* STEP 9.2 - Confirm the token in the response matches the token sent */
	token_len = coap_header_get_token(&reply, token);
	if ((token_len != sizeof(next_token)) ||
	    (memcmp(&next_token, token, sizeof(next_token)) != 0)) {
		LOG_ERR("Invalid token received: 0x%02x%02x\n",
		       token[1], token[0]);
		return 0;
	}

	/* STEP 9.3 - Retrieve the payload and confirm it's nonzero */
	payload = coap_packet_get_payload(&reply, &payload_len);

	if (payload_len > 0) {
		snprintf(temp_buf, MIN(payload_len + 1, sizeof(temp_buf)), "%s", payload);
	} else {
		strcpy(temp_buf, "EMPTY");
	}

	/* STEP 9.4 - Log the header code, token and payload of the response */
	LOG_INF("CoAP response: Code 0x%x, Token 0x%02x%02x, Payload: %s\n",
	       coap_header_get_code(&reply), token[1], token[0], (char *)temp_buf);

	err = handle_device_config_responce(temp_buf);
	if (err < 0){
		LOG_ERR("Failed to handle device config responce");
		return err;
	}

	return 0;
}

static void print_fix_data(struct nrf_modem_gnss_pvt_data_frame *pvt_data)
{
	LOG_INF("Latitude:       %.06f", pvt_data->latitude);
	LOG_INF("Longitude:      %.06f", pvt_data->longitude);
	LOG_INF("Altitude:       %.01f m", pvt_data->altitude);
	LOG_INF("Time (UTC):     %02u:%02u:%02u.%03u",
	       pvt_data->datetime.hour,
	       pvt_data->datetime.minute,
	       pvt_data->datetime.seconds,
	       pvt_data->datetime.ms);
}

static void gnss_event_handler(int event)
{
	int err, num_satellites;

	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
		num_satellites = 0;
		for (int i = 0; i < 12 ; i++) {
			if (pvt_data.sv[i].signal != 0) {
				num_satellites++;
			}
		}
		LOG_INF("Searching. Current satellites: %d", num_satellites);
		err = nrf_modem_gnss_read(&pvt_data, sizeof(pvt_data), NRF_MODEM_GNSS_DATA_PVT);
		if (err) {
			LOG_ERR("nrf_modem_gnss_read failed, err %d", err);
			return;
		}
		if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
			dk_set_led_on(DK_LED1);
			print_fix_data(&pvt_data);
			if (!first_fix) {
				LOG_INF("Time to first fix: %2.1lld s", (k_uptime_get() - gnss_start_time)/1000);
				first_fix = true;
			}
			return;
		}
		/* STEP 5 - Check for the flags indicating GNSS is blocked */
		if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
			LOG_INF("GNSS blocked by LTE activity");
		} else if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
			LOG_INF("Insufficient GNSS time windows");
		}
		break;

	case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
		LOG_INF("GNSS has woken up");
		break;
	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
		LOG_INF("GNSS enter sleep after fix");
		break;
	default:
		break;
	}
}

static int gnss_init_and_start(void)
{

	/* STEP 4 - Set the modem mode to normal */
	if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_NORMAL) != 0) {
		LOG_ERR("Failed to activate GNSS functional mode");
		return -1;
	}

	if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
		LOG_ERR("Failed to set GNSS event handler");
		return -1;
	}

	if (nrf_modem_gnss_fix_interval_set(CONFIG_GNSS_PERIODIC_INTERVAL) != 0) {
		LOG_ERR("Failed to set GNSS fix interval");
		return -1;
	}

	if (nrf_modem_gnss_fix_retry_set(CONFIG_GNSS_PERIODIC_TIMEOUT) != 0) {
		LOG_ERR("Failed to set GNSS fix retry");
		return -1;
	}

	LOG_INF("Starting GNSS");
	if (nrf_modem_gnss_start() != 0) {
		LOG_ERR("Failed to start GNSS");
		return -1;
	}

	gnss_start_time = k_uptime_get();

	return 0;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	/*Send a GET request or PUT request upon button triggers */
	if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK) {
		LOG_INF("Button pressed");
		button_pressed = true;
	}
}

static void on_state_init()
{
	client_get_device_config();
	set_state(STATE_RUNNING);
	if (app_cfg.active_mode)
	{
		set_sub_state(SUB_STATE_ACTIVE_MODE);
	}
	else
	{
		set_sub_state(SUB_STATE_PASSIVE_MODE);
	}
}

static void on_state_running()
{
	
}

static void on_sub_state_active()
{
	
}

static void on_sub_state_passive()
{
	
}

int main(void)
{
	int err;
	int received;

	static bool button_toogle = 1;

	if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LED library");
	}

	err = modem_configure();
	if (err) {
		LOG_ERR("Failed to configure the modem");
		return 0;
	}

	if (dk_buttons_init(button_handler) != 0) {
		LOG_ERR("Failed to initialize the buttons library");
	}

	if (server_resolve() != 0) {
		LOG_INF("Failed to resolve server name");
		return 0;
	}

	if (client_init() != 0) {
		LOG_INF("Failed to initialize client");
		return 0;
	}

	if (gnss_init_and_start() != 0) {
		LOG_ERR("Failed to initialize and start GNSS");
		return 0;
	}

	while (1) {

		switch (state)
		{
		case STATE_INIT:
			on_state_init();
			break;
		case STATE_RUNNING:
			switch (sub_state)
			{
			case SUB_STATE_ACTIVE_MODE:
				on_sub_state_active();
				break;
			case SUB_STATE_PASSIVE_MODE:
				on_sub_state_passive();
				break;
			default:
				break;
			on_state_running();
			}
			break;
		case STATE_SHUTDOWN:
			break;
		
		default:
			LOG_ERR("Unknown state");
			break;
		}

		LOG_INF("Button_toogle: %d", button_toogle ? 1 : 0);
		LOG_INF("Button_pressed: %d", button_pressed ? 1 : 0);

		if (button_pressed)
		{
			button_pressed = false;
	
			if (button_toogle == 1) {
				client_send_request(CONFIG_COAP_RX_RESOURCE, COAP_CONTENT_FORMAT_TEXT_PLAIN, NULL, COAP_METHOD_GET, COAP_TYPE_NON_CON);
			} else {
				client_send_post_request();
			}
			button_toogle = !button_toogle;
		}
		received = recv(sock, coap_buf, sizeof(coap_buf), 0);

		if (received < 0) {
			LOG_ERR("Socket error: %d, exit\n", errno);
			break;
		} else if (received == 0) {
			LOG_INF("Empty datagram\n");
			continue;
		}

		/* Parse the received CoAP packet */
		err = client_handle_response(coap_buf, received);
		if (err < 0) {
			LOG_ERR("Invalid response, exit\n");
			break;
		}
	}

	(void)close(sock);

	return 0;
}
