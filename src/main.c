/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

#define SERVER_HOSTNAME "nordicecho.westeurope.cloudapp.azure.com"
#define SERVER_PORT "2444"

#define MESSAGE_SIZE 256
#define MESSAGE_TO_SEND "Hello"

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

static struct nrf_modem_gnss_pvt_data_frame pvt_data;

static int64_t gnss_start_time;
static bool first_fix = false;

/* STEP 3.1 - Declare buffer to send data in */
static uint8_t gps_data[MESSAGE_SIZE];

static int sock;
static struct sockaddr_storage server;
static uint8_t recv_buf[MESSAGE_SIZE];

static K_SEM_DEFINE(lte_connected, 0, 1);

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
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM
	};

	err = getaddrinfo(SERVER_HOSTNAME, SERVER_PORT, &hints, &result);
	if (err != 0) {
		LOG_INF("ERROR: getaddrinfo failed %d", err);
		return -EIO;
	}

	if (result == NULL) {
		LOG_INF("ERROR: Address not found");
		return -ENOENT;
	}

	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);

	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

	char ipv4_addr[NET_IPV4_ADDR_LEN];
	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr,
		  sizeof(ipv4_addr));
	LOG_INF("IPv4 Address found %s", ipv4_addr);

	freeaddrinfo(result);

	return 0;
}

static int server_connect(void)
{
	int err;
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d.", errno);
		return -errno;
	}

	err = connect(sock, (struct sockaddr *)&server,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connect failed : %d", errno);
		return -errno;
	}
	LOG_INF("Successfully connected to server");

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

	/* STEP 3.2 - Store latitude and longitude in gps_data buffer */
	int err = snprintf(gps_data, MESSAGE_SIZE, "Latitude: %.06f, Longitude: %.06f", pvt_data->latitude, pvt_data->longitude);
	if (err < 0) {
		LOG_ERR("Failed to print to buffer: %d", err);
	}
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
	/* STEP 3.3 - Upon button 1 push, send gps_data */
	switch (has_changed) {
	case DK_BTN1_MSK:
		if (button_state & DK_BTN1_MSK){
			int err = send(sock, &gps_data, sizeof(gps_data), 0);
			if (err < 0) {
				LOG_INF("Failed to send message, %d", errno);
				return;
			}
		}
		break;
	}
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

	if (server_connect() != 0) {
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
		received = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);

		if (received < 0) {
			LOG_ERR("Socket error: %d, exit", errno);
			break;
		} else if (received == 0) {
			break;
		}

		recv_buf[received] = 0;
		LOG_INF("Data received from the server: (%s)", recv_buf);

	}

	(void)close(sock);

	return 0;
}
