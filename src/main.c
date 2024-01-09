/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>


/* STEP 2.2 - Include the header file for the CoAP library */
#include <zephyr/net/coap.h>

/* STEP 4.1 - Define the macro for the message from the board */
// #define MESSAGE_TO_SEND '{"time": "2021-05-01 13:00:00", "latitude": 65.345, "longitude": 28.890, "altitude": 123.456, "accuracy": 12.345}'
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
	/**Device mode: Active or Passive*/
	bool active_mode;
	/**Location search timeout*/
	int location_timeout;
	/**Delay between location search in active mode*/
	int active_wait_timeout;
	/**Delay between location search in passive mode*/
	int passive_wait_timeout;
} app_cfg;

/* STEP 4.2 - Define the macros for the CoAP version and message length */
#define APP_COAP_VERSION 1
#define APP_COAP_MAX_MSG_LEN 1280

/* STEP 5 - Declare the buffer coap_buf to receive the response. */
static uint8_t coap_buf[APP_COAP_MAX_MSG_LEN];

/* STEP 6.1 - Define the CoAP message token next_token */
static uint16_t next_token;

static int sock;
static struct sockaddr_storage server;

K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(Lesson6_Exercise2, LOG_LEVEL_INF);

string sub_state_to_string(enum sub_state_type sub_state)
{
	switch (sub_state)
	{
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
	default:
		return "Unknown"
	}
}

string state_to_string(enum state_type state)
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
		return "Unknown"
	}
}

static void set_state(enum state_type new_state)
{
	if new_state = state{
		LOG_DBG("State: %s", state_to_string(state))
	}
	LOG_DBG("State transition: %s -> %s", 
		state_to_string(state),
		state_to_string(new_state))
}

static void set_sub_state(enum sub_state_type new_sub_state)
{
	if new_sub_state = sub_state{
		LOG_DBG("Sub state: %s", state_to_string(sub_state))
	}
	LOG_DBG("Sub state transition: %s -> %s", 
		sub_state_to_string(sub_state),
		sub_state_to_string(new_sub_state))
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
		LOG_INF("RRC mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
				"Connected" : "Idle");
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

	LOG_INF("Connecting to LTE network");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_INF("Modem could not be configured, error: %d", err);
		return err;
	}

	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");
	dk_set_led_on(DK_LED2);

	return 0;
}

/**@biref Send CoAP GET request. */
static int client_get_send(void)
{
	/* STEP 7.1 - Create the CoAP message*/
	struct coap_packet request;

	next_token++;

	int err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
			       APP_COAP_VERSION, COAP_TYPE_NON_CON,
			       sizeof(next_token), (uint8_t *)&next_token,
			       COAP_METHOD_GET, coap_next_id());
	if (err < 0) {
		LOG_ERR("Failed to create CoAP request, %d\n", err);
		return err;
	}

	/* STEP 7.2 - Add an option specifying the resource path */
	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					(uint8_t *)CONFIG_COAP_RX_RESOURCE,
					strlen(CONFIG_COAP_RX_RESOURCE));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d\n", err);
		return err;
	}

	/* STEP 7.3 - Send the configured CoAP packet */
	err = send(sock, request.data, request.offset, 0);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request, %d\n", errno);
		return -errno;
	}

	LOG_INF("CoAP GET request sent: Token 0x%04x\n", next_token);

	return 0;
}

/**@biref Send CoAP POST request. */
static int client_post_send(void)
{
	int err;
	struct coap_packet request;

	next_token++;

	/* STEP 8.1 - Initialize the CoAP packet and append the resource path */
	err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
				   APP_COAP_VERSION, COAP_TYPE_NON_CON,
				   sizeof(next_token), (uint8_t *)&next_token,
				   COAP_METHOD_POST, coap_next_id());
	if (err < 0) {
		LOG_ERR("Failed to create CoAP request, %d\n", err);
		return err;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					(uint8_t *)CONFIG_COAP_TX_RESOURCE,
					strlen(CONFIG_COAP_TX_RESOURCE));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d\n", err);
		return err;
	}

	/* STEP 8.2 - Append the content format as plain text */
	const uint8_t text_plain = COAP_CONTENT_FORMAT_TEXT_PLAIN;
	err = coap_packet_append_option(&request, COAP_OPTION_CONTENT_FORMAT,
					&text_plain,
					sizeof(text_plain));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d\n", err);
		return err;
	}

	/* STEP 8.3 - Add the payload to the message */
	err = coap_packet_append_payload_marker(&request);
	if (err < 0) {
		LOG_ERR("Failed to append payload marker, %d\n", err);
		return err;
	}

	err = coap_packet_append_payload(&request, (uint8_t *)MESSAGE_TO_SEND, sizeof(MESSAGE_TO_SEND));
	if (err < 0) {
		LOG_ERR("Failed to append payload, %d\n", err);
		return err;
	}

	err = send(sock, request.data, request.offset, 0);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request, %d\n", errno);
		return -errno;
	}

	LOG_INF("CoAP POST request sent: Token 0x%04x\n", next_token);

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

	return 0;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	/* STEP 10 - Send a GET request or PUT request upon button triggers */
	#if defined (CONFIG_BOARD_NRF9160DK_NRF9160_NS)
	if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK) {
		client_get_send();
	} else if (has_changed & DK_BTN2_MSK && button_state & DK_BTN2_MSK) {
		client_post_send();
	}
	#elif defined (CONFIG_BOARD_THINGY91_NRF9160_NS)
	static bool toogle = 1;
	if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK) {
		if (toogle == 1) {
			client_get_send();
		} else {
			client_post_send();
		}
		toogle = !toogle;
	}
	#endif
}

static void on_state_init()
{
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
		received = recv(sock, coap_buf, sizeof(coap_buf), 0);

		if (received < 0) {
			LOG_ERR("Socket error: %d, exit\n", errno);
			break;
		} else if (received == 0) {
			LOG_INF("Empty datagram\n");
			continue;
		}

		/* STEP 12 - Parse the received CoAP packet */
		err = client_handle_response(coap_buf, received);
		if (err < 0) {
			LOG_ERR("Invalid response, exit\n");
			break;
		}
	}

	(void)close(sock);

	return 0;
}
