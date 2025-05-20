/*
 * Espressif Systems Wireless LAN device driver
 *
 * Copyright (C) 2015-2021 Espressif Systems (Shanghai) PTE LTD
 *
 * This software file (the "File") is distributed by Espressif Systems (Shanghai)
 * PTE LTD under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */


#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "rpc_slave_if.h"
#include "string.h"
#include "os_wrapper.h"
#include "rpc_wrap.h"
#include "esp_hosted_rpc.h"
#include "esp_log.h"
#include "esp_hosted_wifi_config.h"
#include "esp_hosted_api.h"
#include "esp_hosted_transport.h"

DEFINE_LOG_TAG(rpc_wrap);

uint8_t restart_after_slave_ota = 0;

#define WIFI_VENDOR_IE_ELEMENT_ID                         0xDD
#define OFFSET                                            4
#define VENDOR_OUI_0                                      1
#define VENDOR_OUI_1                                      2
#define VENDOR_OUI_2                                      3
#define VENDOR_OUI_TYPE                                   22
#define CHUNK_SIZE                                        1400
#define OTA_BEGIN_RSP_TIMEOUT_SEC                         15
#define OTA_FROM_WEB_URL                                  1



static ctrl_cmd_t * RPC_DEFAULT_REQ(void)
{
  ctrl_cmd_t *new_req = (ctrl_cmd_t*)g_h.funcs->_h_calloc(1, sizeof(ctrl_cmd_t));
  assert(new_req);
  new_req->msg_type = RPC_TYPE__Req;
  new_req->rpc_rsp_cb = NULL;
  new_req->rsp_timeout_sec = DEFAULT_RPC_RSP_TIMEOUT; /* 5 sec */
  /* new_req->wait_prev_cmd_completion = WAIT_TIME_B2B_RPC_REQ; */
  return new_req;
}


#define CLEANUP_RPC(msg) do {                            \
  if (msg) {                                             \
    if (msg->app_free_buff_hdl) {                        \
      if (msg->app_free_buff_func) {                     \
        msg->app_free_buff_func(msg->app_free_buff_hdl); \
        msg->app_free_buff_hdl = NULL;                   \
      }                                                  \
    }                                                    \
    g_h.funcs->_h_free(msg);                             \
    msg = NULL;                                          \
  }                                                      \
} while(0);

#define YES                                               1
#define NO                                                0
#define MIN_TIMESTAMP_STR_SIZE                            30
#define HEARTBEAT_DURATION_SEC                            20


typedef struct {
	int event;
	rpc_rsp_cb_t fun;
} event_callback_table_t;

int rpc_init(void)
{
	ESP_LOGD(TAG, "%s", __func__);
	return rpc_slaveif_init();
}

int rpc_deinit(void)
{
	ESP_LOGD(TAG, "%s", __func__);
	return rpc_slaveif_deinit();
}

static char * get_timestamp_str(char *str, uint16_t str_size)
{
	if (str && str_size>=MIN_TIMESTAMP_STR_SIZE) {
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		sprintf(str, "%d-%02d-%02d %02d:%02d:%02d > ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		return str;
	}
	return NULL;
}

static int rpc_event_callback(ctrl_cmd_t * app_event)
{
	char ts[MIN_TIMESTAMP_STR_SIZE] = {'\0'};

	ESP_LOGV(TAG, "%u",app_event->msg_id);
	if (!app_event || (app_event->msg_type != RPC_TYPE__Event)) {
		if (app_event)
			ESP_LOGE(TAG, "Recvd msg [0x%x] is not event",app_event->msg_type);
		goto fail_parsing;
	}

	if ((app_event->msg_id <= RPC_ID__Event_Base) ||
	    (app_event->msg_id >= RPC_ID__Event_Max)) {
		ESP_LOGE(TAG, "Event Msg ID[0x%x] is not correct",app_event->msg_id);
		goto fail_parsing;
	}

	switch(app_event->msg_id) {

		case RPC_ID__Event_ESPInit: {
			ESP_LOGD(TAG, "Received Slave ESP Init");
			break;
		} case RPC_ID__Event_Heartbeat: {
			ESP_LOGV(TAG, "%s ESP EVENT: Heartbeat event [%lu]",
				get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE),
					(long unsigned int)app_event->u.e_heartbeat.hb_num);
			break;
		} case RPC_ID__Event_AP_StaConnected: {
			wifi_event_ap_staconnected_t *p_e = &app_event->u.e_wifi_ap_staconnected;

			if (strlen((char*)p_e->mac)) {
				ESP_LOGV(TAG, "%s ESP EVENT: SoftAP mode: connected station",
					get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE));
				g_h.funcs->_h_event_wifi_post(WIFI_EVENT_AP_STACONNECTED,
					p_e, sizeof(wifi_event_ap_staconnected_t), HOSTED_BLOCK_MAX);
			}
			break;
		} case RPC_ID__Event_AP_StaDisconnected: {
			wifi_event_ap_stadisconnected_t *p_e = &app_event->u.e_wifi_ap_stadisconnected;
			if (strlen((char*)p_e->mac)) {
				ESP_LOGV(TAG, "%s ESP EVENT: SoftAP mode: disconnected MAC",
					get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE));
				g_h.funcs->_h_event_wifi_post(WIFI_EVENT_AP_STADISCONNECTED,
					p_e, sizeof(wifi_event_ap_stadisconnected_t), HOSTED_BLOCK_MAX);
			}
			break;
		} case RPC_ID__Event_StaConnected: {
			ESP_LOGV(TAG, "%s ESP EVENT: Station mode: Connected",
				get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE));
			wifi_event_sta_connected_t *p_e = &app_event->u.e_wifi_sta_connected;
			g_h.funcs->_h_event_wifi_post(WIFI_EVENT_STA_CONNECTED,
				p_e, sizeof(wifi_event_sta_connected_t), HOSTED_BLOCK_MAX);
			break;
		} case RPC_ID__Event_StaDisconnected: {
			ESP_LOGV(TAG, "%s ESP EVENT: Station mode: Disconnected",
				get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE));
			wifi_event_sta_disconnected_t *p_e = &app_event->u.e_wifi_sta_disconnected;
			g_h.funcs->_h_event_wifi_post(WIFI_EVENT_STA_DISCONNECTED,
				p_e, sizeof(wifi_event_sta_disconnected_t), HOSTED_BLOCK_MAX);
			break;
		} case RPC_ID__Event_WifiEventNoArgs: {
			int wifi_event_id = app_event->u.e_wifi_simple.wifi_event_id;

			switch (wifi_event_id) {

			case WIFI_EVENT_STA_START:
				ESP_LOGV(TAG, "%s ESP EVENT: WiFi Event[%s]",
					get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE), "WIFI_EVENT_STA_START");
				break;
			case WIFI_EVENT_STA_STOP:
				ESP_LOGV(TAG, "%s ESP EVENT: WiFi Event[%s]",
					get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE), "WIFI_EVENT_STA_STOP");
				break;

			case WIFI_EVENT_AP_START:
				ESP_LOGD(TAG,"ESP EVENT: softap started");
				break;

			case WIFI_EVENT_AP_STOP:
				ESP_LOGD(TAG,"ESP EVENT: softap stopped");
				break;

			default:
				ESP_LOGV(TAG, "%s ESP EVENT: WiFi Event[%x]",
					get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE), wifi_event_id);
				break;
			} /* inner switch case */
			g_h.funcs->_h_event_wifi_post(wifi_event_id, 0, 0, HOSTED_BLOCK_MAX);

			break;
		} case RPC_ID__Event_StaScanDone: {
			wifi_event_sta_scan_done_t *p_e = &app_event->u.e_wifi_sta_scan_done;
			ESP_LOGV(TAG, "%s ESP EVENT: StaScanDone",
					get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE));
			ESP_LOGV(TAG, "scan: status: %lu number:%u scan_id:%u", p_e->status, p_e->number, p_e->scan_id);
			g_h.funcs->_h_event_wifi_post(WIFI_EVENT_SCAN_DONE,
				p_e, sizeof(wifi_event_sta_scan_done_t), HOSTED_BLOCK_MAX);
			break;
		} default: {
			ESP_LOGW(TAG, "%s Invalid event[0x%x] to parse",
				get_timestamp_str(ts, MIN_TIMESTAMP_STR_SIZE), app_event->msg_id);
			break;
		}
	}
	CLEANUP_RPC(app_event);
	return SUCCESS;

fail_parsing:
	CLEANUP_RPC(app_event);
	return FAILURE;
}

static int process_failed_responses(ctrl_cmd_t *app_msg)
{
	uint8_t request_failed_flag = true;
	int result = app_msg->resp_event_status;

	/* Identify general issue, common for all control requests */
	/* Map results to a matching ESP_ERR_ code */
	switch (app_msg->resp_event_status) {
		case RPC_ERR_REQ_IN_PROG:
			ESP_LOGE(TAG, "Error reported: Command In progress, Please wait");
			break;
		case RPC_ERR_REQUEST_TIMEOUT:
			ESP_LOGE(TAG, "Error reported: Response Timeout");
			break;
		case RPC_ERR_MEMORY_FAILURE:
			ESP_LOGE(TAG, "Error reported: Memory allocation failed");
			break;
		case RPC_ERR_UNSUPPORTED_MSG:
			ESP_LOGE(TAG, "Error reported: Unsupported control msg");
			break;
		case RPC_ERR_INCORRECT_ARG:
			ESP_LOGE(TAG, "Error reported: Invalid or out of range parameter values");
			break;
		case RPC_ERR_PROTOBUF_ENCODE:
			ESP_LOGE(TAG, "Error reported: Protobuf encode failed");
			break;
		case RPC_ERR_PROTOBUF_DECODE:
			ESP_LOGE(TAG, "Error reported: Protobuf decode failed");
			break;
		case RPC_ERR_SET_ASYNC_CB:
			ESP_LOGE(TAG, "Error reported: Failed to set aync callback");
			break;
		case RPC_ERR_TRANSPORT_SEND:
			ESP_LOGE(TAG, "Error reported: Problem while sending data on serial driver");
			break;
		case RPC_ERR_SET_SYNC_SEM:
			ESP_LOGE(TAG, "Error reported: Failed to set sync sem");
			break;
		default:
			request_failed_flag = false;
			break;
	}

	/* if control request failed, no need to proceed for response checking */
	if (request_failed_flag)
		return result;

	/* Identify control request specific issue */
	switch (app_msg->msg_id) {

		case RPC_ID__Resp_OTAEnd:
		case RPC_ID__Resp_OTABegin:
		case RPC_ID__Resp_OTAWrite: {
			/* intentional fallthrough */
			ESP_LOGE(TAG, "OTA procedure failed");
			break;
		} default: {
			ESP_LOGD(TAG, "Got Hosted Control Response with resp code %d", result);
			break;
		}
	}
	return result;
}


int rpc_unregister_event_callbacks(void)
{
	int ret = SUCCESS;
	int evt = 0;
	for (evt=RPC_ID__Event_Base+1; evt<RPC_ID__Event_Max; evt++) {
		if (CALLBACK_SET_SUCCESS != reset_event_callback(evt) ) {
			ESP_LOGV(TAG, "reset event callback failed for event[%u]", evt);
			ret = FAILURE;
		}
	}
	return ret;
}

int rpc_register_event_callbacks(void)
{
	int ret = SUCCESS;
	int evt = 0;

	event_callback_table_t events[] = {
		{ RPC_ID__Event_ESPInit,                   rpc_event_callback },
#if 0
		{ RPC_ID__Event_Heartbeat,                 rpc_event_callback },
#endif
		{ RPC_ID__Event_AP_StaConnected,           rpc_event_callback },
		{ RPC_ID__Event_AP_StaDisconnected,        rpc_event_callback },
		{ RPC_ID__Event_WifiEventNoArgs,           rpc_event_callback },
		{ RPC_ID__Event_StaScanDone,               rpc_event_callback },
		{ RPC_ID__Event_StaConnected,              rpc_event_callback },
		{ RPC_ID__Event_StaDisconnected,           rpc_event_callback },
	};

	for (evt=0; evt<sizeof(events)/sizeof(event_callback_table_t); evt++) {
		if (CALLBACK_SET_SUCCESS != set_event_callback(events[evt].event, events[evt].fun) ) {
			ESP_LOGE(TAG, "event callback register failed for event[%u]", events[evt].event);
			ret = FAILURE;
			break;
		}
	}
	return ret;
}


int rpc_rsp_callback(ctrl_cmd_t * app_resp)
{
	int response = ESP_FAIL; // default response

	uint16_t i = 0;
	if (!app_resp || (app_resp->msg_type != RPC_TYPE__Resp)) {
		if (app_resp)
			ESP_LOGE(TAG, "Recvd Msg[0x%x] is not response",app_resp->msg_type);
		goto fail_resp;
	}

	if ((app_resp->msg_id <= RPC_ID__Resp_Base) || (app_resp->msg_id >= RPC_ID__Resp_Max)) {
		ESP_LOGE(TAG, "Response Msg ID[0x%x] is not correct",app_resp->msg_id);
		goto fail_resp;
	}

	if (app_resp->resp_event_status != SUCCESS) {
		response = process_failed_responses(app_resp);
		goto fail_resp;
	}

	switch(app_resp->msg_id) {

	case RPC_ID__Resp_GetMACAddress: {
		ESP_LOGV(TAG, "mac address is [" MACSTR "]", MAC2STR(app_resp->u.wifi_mac.mac));
		break;
	} case RPC_ID__Resp_SetMacAddress : {
		ESP_LOGV(TAG, "MAC address is set");
		break;
	} case RPC_ID__Resp_GetWifiMode : {
		ESP_LOGV(TAG, "wifi mode is : ");
		switch (app_resp->u.wifi_mode.mode) {
			case WIFI_MODE_STA:     ESP_LOGV(TAG, "station");        break;
			case WIFI_MODE_AP:      ESP_LOGV(TAG, "softap");         break;
			case WIFI_MODE_APSTA:   ESP_LOGV(TAG, "station+softap"); break;
			case WIFI_MODE_NULL:    ESP_LOGV(TAG, "none");           break;
			default:                ESP_LOGV(TAG, "unknown");        break;
		}
		break;
	} case RPC_ID__Resp_SetWifiMode : {
		ESP_LOGV(TAG, "wifi mode is set");
		break;
	} case RPC_ID__Resp_WifiSetPs: {
		ESP_LOGV(TAG, "Wifi power save mode set");
		break;
	} case RPC_ID__Resp_WifiGetPs: {
		ESP_LOGV(TAG, "Wifi power save mode is: ");

		switch(app_resp->u.wifi_ps.ps_mode) {
			case WIFI_PS_MIN_MODEM:
				ESP_LOGV(TAG, "Min");
				break;
			case WIFI_PS_MAX_MODEM:
				ESP_LOGV(TAG, "Max");
				break;
			default:
				ESP_LOGV(TAG, "Invalid");
				break;
		}
		break;
	} case RPC_ID__Resp_OTABegin : {
		ESP_LOGV(TAG, "OTA begin success");
		break;
	} case RPC_ID__Resp_OTAWrite : {
		ESP_LOGV(TAG, "OTA write success");
		break;
	} case RPC_ID__Resp_OTAEnd : {
		ESP_LOGV(TAG, "OTA end success");
		break;
	} case RPC_ID__Resp_WifiSetMaxTxPower: {
		ESP_LOGV(TAG, "Set wifi max tx power success");
		break;
	} case RPC_ID__Resp_WifiGetMaxTxPower: {
		ESP_LOGV(TAG, "wifi curr tx power : %d",
				app_resp->u.wifi_tx_power.power);
		break;
	} case RPC_ID__Resp_ConfigHeartbeat: {
		ESP_LOGV(TAG, "Heartbeat operation successful");
		break;
	} case RPC_ID__Resp_WifiScanGetApNum: {
		ESP_LOGV(TAG, "Num Scanned APs: %u",
				app_resp->u.wifi_scan_ap_list.number);
		break;
	} case RPC_ID__Resp_WifiScanGetApRecords: {
		wifi_scan_ap_list_t * p_a = &app_resp->u.wifi_scan_ap_list;
		wifi_ap_record_t *list = p_a->out_list;

		if (!p_a->number) {
			ESP_LOGV(TAG, "No AP info found");
			goto finish_resp;
		}
		ESP_LOGV(TAG, "Num AP records: %u",
				app_resp->u.wifi_scan_ap_list.number);
		if (!list) {
			ESP_LOGV(TAG, "Failed to get scanned AP list");
			goto fail_resp;
		} else {

			ESP_LOGV(TAG, "Number of available APs is %d", p_a->number);
			for (i=0; i<p_a->number; i++) {
				ESP_LOGV(TAG, "%d) ssid \"%s\" bssid \"%s\" rssi \"%d\" channel \"%d\" auth mode \"%d\"",\
						i, list[i].ssid, list[i].bssid, list[i].rssi,
						list[i].primary, list[i].authmode);
			}
		}
		break;
	}
	case RPC_ID__Resp_WifiScanGetApRecord:
	case RPC_ID__Resp_WifiInit:
	case RPC_ID__Resp_WifiDeinit:
	case RPC_ID__Resp_WifiStart:
	case RPC_ID__Resp_WifiStop:
	case RPC_ID__Resp_WifiConnect:
	case RPC_ID__Resp_WifiDisconnect:
	case RPC_ID__Resp_WifiGetConfig:
	case RPC_ID__Resp_WifiScanStart:
	case RPC_ID__Resp_WifiScanStop:
	case RPC_ID__Resp_WifiClearApList:
	case RPC_ID__Resp_WifiRestore:
	case RPC_ID__Resp_WifiClearFastConnect:
	case RPC_ID__Resp_WifiDeauthSta:
	case RPC_ID__Resp_WifiStaGetApInfo:
	case RPC_ID__Resp_WifiSetConfig:
	case RPC_ID__Resp_WifiSetStorage:
	case RPC_ID__Resp_WifiSetBandwidth:
	case RPC_ID__Resp_WifiGetBandwidth:
	case RPC_ID__Resp_WifiSetChannel:
	case RPC_ID__Resp_WifiGetChannel:
	case RPC_ID__Resp_WifiSetCountryCode:
	case RPC_ID__Resp_WifiGetCountryCode:
	case RPC_ID__Resp_WifiSetCountry:
	case RPC_ID__Resp_WifiGetCountry:
	case RPC_ID__Resp_WifiApGetStaList:
	case RPC_ID__Resp_WifiApGetStaAid:
	case RPC_ID__Resp_WifiStaGetRssi:
	case RPC_ID__Resp_WifiSetProtocol:
	case RPC_ID__Resp_WifiGetProtocol:
	case RPC_ID__Resp_WifiStaGetNegotiatedPhymode:
	case RPC_ID__Resp_WifiStaGetAid:
	case RPC_ID__Resp_WifiSetProtocols:
	case RPC_ID__Resp_WifiGetProtocols:
	case RPC_ID__Resp_WifiSetBandwidths:
	case RPC_ID__Resp_WifiGetBandwidths:
	case RPC_ID__Resp_WifiSetBand:
	case RPC_ID__Resp_WifiGetBand:
	case RPC_ID__Resp_WifiSetBandMode:
	case RPC_ID__Resp_WifiGetBandMode:
	case RPC_ID__Resp_GetCoprocessorFwVersion: {
		/* Intended fallthrough */
		break;
	} default: {
		ESP_LOGE(TAG, "Invalid Response[0x%x] to parse", app_resp->msg_id);
		goto fail_resp;
	}

	} //switch

finish_resp:
	// extract response from app_resp
	response = app_resp->resp_event_status;
	CLEANUP_RPC(app_resp);
	return response;

fail_resp:
	CLEANUP_RPC(app_resp);
	return response;
}

int rpc_get_wifi_mode(void)
{
	/* implemented Asynchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();

	/* register callback for reply */
	req->rpc_rsp_cb = rpc_rsp_callback;

	rpc_slaveif_wifi_get_mode(req);

	return SUCCESS;
}


int rpc_set_wifi_mode(wifi_mode_t mode)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_mode.mode = mode;
	resp = rpc_slaveif_wifi_set_mode(req);

	return rpc_rsp_callback(resp);
}

int rpc_set_wifi_mode_station(void)
{
	return rpc_set_wifi_mode(WIFI_MODE_STA);
}

int rpc_set_wifi_mode_softap(void)
{
	return rpc_set_wifi_mode(WIFI_MODE_AP);
}

int rpc_set_wifi_mode_station_softap(void)
{
	return rpc_set_wifi_mode(WIFI_MODE_APSTA);
}

int rpc_set_wifi_mode_none(void)
{
	return rpc_set_wifi_mode(WIFI_MODE_NULL);
}

int rpc_wifi_get_mac(wifi_interface_t mode, uint8_t out_mac[6])
{
	ctrl_cmd_t *resp = NULL;

	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();

	req->u.wifi_mac.mode = mode;
	resp = rpc_slaveif_wifi_get_mac(req);

	if (resp && resp->resp_event_status == SUCCESS) {

		g_h.funcs->_h_memcpy(out_mac, resp->u.wifi_mac.mac, BSSID_BYTES_SIZE);
		ESP_LOGV(TAG, "mac address is [" MACSTR "]", MAC2STR(out_mac));
	}
	return rpc_rsp_callback(resp);
}

int rpc_station_mode_get_mac(uint8_t mac[6])
{
	return rpc_wifi_get_mac(WIFI_MODE_STA, mac);
}

int rpc_wifi_set_mac(wifi_interface_t mode, const uint8_t mac[6])
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_mac.mode = mode;
	g_h.funcs->_h_memcpy(req->u.wifi_mac.mac, mac, BSSID_BYTES_SIZE);

	resp = rpc_slaveif_wifi_set_mac(req);
	return rpc_rsp_callback(resp);
}


int rpc_softap_mode_get_mac_addr(uint8_t mac[6])
{
	return rpc_wifi_get_mac(WIFI_MODE_AP, mac);
}

int rpc_ota_begin(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	/* OTA begin takes some time to clear the partition */
	req->rsp_timeout_sec = OTA_BEGIN_RSP_TIMEOUT_SEC;

	resp = rpc_slaveif_ota_begin(req);

	return rpc_rsp_callback(resp);
}

int rpc_ota_write(uint8_t* ota_data, uint32_t ota_data_len)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.ota_write.ota_data = ota_data;
	req->u.ota_write.ota_data_len = ota_data_len;

	resp = rpc_slaveif_ota_write(req);

	return rpc_rsp_callback(resp);
}

int rpc_ota_end(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_ota_end(req);

	return rpc_rsp_callback(resp);
}

esp_err_t rpc_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *ver_info)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_get_coprocessor_fwversion(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		ver_info->major1 = resp->u.coprocessor_fwversion.major1;
		ver_info->minor1 = resp->u.coprocessor_fwversion.minor1;
		ver_info->patch1 = resp->u.coprocessor_fwversion.patch1;
	}

	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_max_tx_power(int8_t in_power)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_tx_power.power = in_power;
	resp = rpc_slaveif_wifi_set_max_tx_power(req);

	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_max_tx_power(int8_t *power)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_get_max_tx_power(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*power = resp->u.wifi_tx_power.power;
	}
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *phymode)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_sta_get_negotiated_phymode(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*phymode = resp->u.wifi_sta_get_negotiated_phymode.phymode;
	}
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_sta_get_aid(uint16_t *aid)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_sta_get_aid(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*aid = resp->u.wifi_sta_get_aid.aid;
	}
	return rpc_rsp_callback(resp);
}

#if H_WIFI_DUALBAND_SUPPORT
esp_err_t rpc_wifi_set_band(wifi_band_t band)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_band = band;
	resp = rpc_slaveif_wifi_set_band(req);

	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_get_band(wifi_band_t *band)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_get_band(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*band = resp->u.wifi_band;
	}
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_set_band_mode(wifi_band_mode_t band_mode)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_band_mode = band_mode;
	resp = rpc_slaveif_wifi_set_band_mode(req);

	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_get_band_mode(wifi_band_mode_t *band_mode)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_get_band_mode(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*band_mode = resp->u.wifi_band_mode;
	}
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_set_protocols(wifi_interface_t ifx, wifi_protocols_t *protocols)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_protocols.ifx = ifx;
	req->u.wifi_protocols.ghz_2g = protocols->ghz_2g;
	req->u.wifi_protocols.ghz_5g = protocols->ghz_5g;

	resp = rpc_slaveif_wifi_set_protocols(req);
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_get_protocols(wifi_interface_t ifx, wifi_protocols_t *protocols)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_protocols.ifx = ifx;

	resp = rpc_slaveif_wifi_get_protocols(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		protocols->ghz_2g = resp->u.wifi_protocols.ghz_2g;
		protocols->ghz_5g = resp->u.wifi_protocols.ghz_5g;
	}
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_set_bandwidths(wifi_interface_t ifx, wifi_bandwidths_t *bw)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_bandwidths.ifx = ifx;
	req->u.wifi_bandwidths.ghz_2g = bw->ghz_2g;
	req->u.wifi_bandwidths.ghz_5g = bw->ghz_5g;

	resp = rpc_slaveif_wifi_set_bandwidths(req);
	return rpc_rsp_callback(resp);
}

esp_err_t rpc_wifi_get_bandwidths(wifi_interface_t ifx, wifi_bandwidths_t *bw)
{

	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_bandwidths.ifx = ifx;

	resp = rpc_slaveif_wifi_get_bandwidths(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		bw->ghz_2g = resp->u.wifi_bandwidths.ghz_2g;
		bw->ghz_5g = resp->u.wifi_bandwidths.ghz_5g;
	}
	return rpc_rsp_callback(resp);
}
#endif

int rpc_config_heartbeat(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *resp = NULL;
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	req->u.e_heartbeat.enable = YES;
	req->u.e_heartbeat.duration = HEARTBEAT_DURATION_SEC;

	resp = rpc_slaveif_config_heartbeat(req);

	return rpc_rsp_callback(resp);
}

int rpc_disable_heartbeat(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *resp = NULL;
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	req->u.e_heartbeat.enable = NO;

	resp = rpc_slaveif_config_heartbeat(req);

	return rpc_rsp_callback(resp);
}

int rpc_wifi_init(const wifi_init_config_t *arg)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!arg)
		return FAILURE;

	g_h.funcs->_h_memcpy(&req->u.wifi_init_config, (void*)arg, sizeof(wifi_init_config_t));
	resp = rpc_slaveif_wifi_init(req);

	return rpc_rsp_callback(resp);
}

int rpc_wifi_deinit(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_deinit(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_mode(wifi_mode_t mode)
{
	return rpc_set_wifi_mode(mode);
}

int rpc_wifi_get_mode(wifi_mode_t* mode)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!mode)
		return FAILURE;

	resp = rpc_slaveif_wifi_get_mode(req);

	if (resp && resp->resp_event_status == SUCCESS) {
		*mode = resp->u.wifi_mode.mode;
	}

	return rpc_rsp_callback(resp);
}

int rpc_wifi_start(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_start(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_stop(void)
{
	if (restart_after_slave_ota)
		return 0;

	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_stop(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_connect(void)
{
#if 1
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_connect(req);
	return rpc_rsp_callback(resp);
	return 0;
#else
	/* implemented asynchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->rpc_rsp_cb = rpc_rsp_callback;
	ESP_LOGE(TAG, "Async call registerd: %p", rpc_rsp_callback);

	rpc_slaveif_wifi_connect(req);

	return SUCCESS;
#endif
}

int rpc_wifi_disconnect(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_disconnect(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!conf)
		return FAILURE;

	g_h.funcs->_h_memcpy(&req->u.wifi_config.u, conf, sizeof(wifi_config_t));

	req->u.wifi_config.iface = interface;
	resp = rpc_slaveif_wifi_set_config(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!conf)
		return FAILURE;

	req->u.wifi_config.iface = interface;

	resp = rpc_slaveif_wifi_get_config(req);

	g_h.funcs->_h_memcpy(conf, &resp->u.wifi_config.u, sizeof(wifi_config_t));

	return rpc_rsp_callback(resp);
}

int rpc_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (config) {
		g_h.funcs->_h_memcpy(&req->u.wifi_scan_config.cfg, config, sizeof(wifi_scan_config_t));
		req->u.wifi_scan_config.cfg_set = 1;
	}

	req->u.wifi_scan_config.block = block;

	resp = rpc_slaveif_wifi_scan_start(req);

	return rpc_rsp_callback(resp);
}

int rpc_wifi_scan_stop(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;
	ESP_LOGV(TAG, "scan stop");

	resp = rpc_slaveif_wifi_scan_stop(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_scan_get_ap_num(uint16_t *number)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!number)
		return FAILURE;

	resp = rpc_slaveif_wifi_scan_get_ap_num(req);

	if (resp && resp->resp_event_status == SUCCESS) {
		*number = resp->u.wifi_scan_ap_list.number;
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_scan_get_ap_record(wifi_ap_record_t *ap_record)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!ap_record)
		return FAILURE;

	resp = rpc_slaveif_wifi_scan_get_ap_record(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		g_h.funcs->_h_memcpy(ap_record, &resp->u.wifi_ap_record, sizeof(wifi_ap_record_t));
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!number || !*number || !ap_records)
		return FAILURE;

	g_h.funcs->_h_memset(ap_records, 0, (*number)*sizeof(wifi_ap_record_t));

	req->u.wifi_scan_ap_list.number = *number;
	resp = rpc_slaveif_wifi_scan_get_ap_records(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		ESP_LOGV(TAG, "num: %u",resp->u.wifi_scan_ap_list.number);

		g_h.funcs->_h_memcpy(ap_records, resp->u.wifi_scan_ap_list.out_list,
				resp->u.wifi_scan_ap_list.number * sizeof(wifi_ap_record_t));
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_clear_ap_list(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_clear_ap_list(req);
	return rpc_rsp_callback(resp);
}


int rpc_wifi_restore(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_restore(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_clear_fast_connect(void)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_clear_fast_connect(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_deauth_sta(uint16_t aid)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_deauth_sta.aid = aid;
	resp = rpc_slaveif_wifi_deauth_sta(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!ap_info)
		return FAILURE;

	resp = rpc_slaveif_wifi_sta_get_ap_info(req);

	if (resp && resp->resp_event_status == SUCCESS) {
		g_h.funcs->_h_memcpy(ap_info, resp->u.wifi_scan_ap_list.out_list,
				sizeof(wifi_ap_record_t));
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_ps(wifi_ps_type_t type)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (type > WIFI_PS_MAX_MODEM)
		return FAILURE;

	req->u.wifi_ps.ps_mode = type;

	resp = rpc_slaveif_wifi_set_ps(req);

	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_ps(wifi_ps_type_t *type)
{
	if (!type)
		return FAILURE;

	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!type)
		return FAILURE;

	resp = rpc_slaveif_wifi_get_ps(req);

	*type = resp->u.wifi_ps.ps_mode;

	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_storage(wifi_storage_t storage)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_storage = storage;
	resp = rpc_slaveif_wifi_set_storage(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_bandwidth.ifx = ifx;
	req->u.wifi_bandwidth.bw = bw;
	resp = rpc_slaveif_wifi_set_bandwidth(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t *bw)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!bw)
		return FAILURE;

	req->u.wifi_bandwidth.ifx = ifx;
	resp = rpc_slaveif_wifi_get_bandwidth(req);

	if (resp && resp->resp_event_status == SUCCESS) {
		*bw = resp->u.wifi_bandwidth.bw;
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_channel(uint8_t primary, wifi_second_chan_t second)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_channel.primary = primary;
	req->u.wifi_channel.second = second;
	resp = rpc_slaveif_wifi_set_channel(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if ((!primary) || (!second))
		return FAILURE;

	resp = rpc_slaveif_wifi_get_channel(req);

	if (resp && resp->resp_event_status == SUCCESS) {
		*primary = resp->u.wifi_channel.primary;
		*second = resp->u.wifi_channel.second;
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_country_code(const char *country, bool ieee80211d_enabled)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!country)
		return FAILURE;

	memcpy(&req->u.wifi_country_code.cc[0], country, sizeof(req->u.wifi_country_code.cc));
	req->u.wifi_country_code.ieee80211d_enabled = ieee80211d_enabled;
	resp = rpc_slaveif_wifi_set_country_code(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_country_code(char *country)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!country)
		return FAILURE;

	resp = rpc_slaveif_wifi_get_country_code(req);

	if (resp && resp->resp_event_status == SUCCESS) {
		memcpy(country, &resp->u.wifi_country_code.cc[0], sizeof(resp->u.wifi_country_code.cc));
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_country(const wifi_country_t *country)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!country)
		return FAILURE;

	memcpy(&req->u.wifi_country.cc[0], &country->cc[0], sizeof(country->cc));
	req->u.wifi_country.schan        = country->schan;
	req->u.wifi_country.nchan        = country->nchan;
	req->u.wifi_country.max_tx_power = country->max_tx_power;
	req->u.wifi_country.policy       = country->policy;

	resp = rpc_slaveif_wifi_set_country(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_country(wifi_country_t *country)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!country)
		return FAILURE;

	resp = rpc_slaveif_wifi_get_country(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		memcpy(&country->cc[0], &resp->u.wifi_country.cc[0], sizeof(resp->u.wifi_country.cc));
		country->schan        = resp->u.wifi_country.schan;
		country->nchan        = resp->u.wifi_country.nchan;
		country->max_tx_power = resp->u.wifi_country.max_tx_power;
		country->policy       = resp->u.wifi_country.policy;
	}
	return rpc_rsp_callback(resp);
}

int rpc_wifi_ap_get_sta_list(wifi_sta_list_t *sta)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!sta)
		return FAILURE;

	resp = rpc_slaveif_wifi_ap_get_sta_list(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		for (int i = 0; i < ESP_WIFI_MAX_CONN_NUM; i++) {
			memcpy(sta->sta[i].mac, resp->u.wifi_ap_sta_list.sta[i].mac, 6);
			sta->sta[i].rssi = resp->u.wifi_ap_sta_list.sta[i].rssi;
			sta->sta[i].phy_11b = resp->u.wifi_ap_sta_list.sta[i].phy_11b;
			sta->sta[i].phy_11g = resp->u.wifi_ap_sta_list.sta[i].phy_11g;
			sta->sta[i].phy_11n = resp->u.wifi_ap_sta_list.sta[i].phy_11n;
			sta->sta[i].phy_lr = resp->u.wifi_ap_sta_list.sta[i].phy_lr;
			sta->sta[i].phy_11ax = resp->u.wifi_ap_sta_list.sta[i].phy_11ax;
			sta->sta[i].is_mesh_child = resp->u.wifi_ap_sta_list.sta[i].is_mesh_child;
			sta->sta[i].reserved = resp->u.wifi_ap_sta_list.sta[i].reserved;

		}
	}
	sta->num = resp->u.wifi_ap_sta_list.num;

	return rpc_rsp_callback(resp);
}

int rpc_wifi_ap_get_sta_aid(const uint8_t mac[6], uint16_t *aid)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!mac || !aid)
		return FAILURE;

	memcpy(&req->u.wifi_ap_get_sta_aid.mac[0], &mac[0], MAC_SIZE_BYTES);

	resp = rpc_slaveif_wifi_ap_get_sta_aid(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*aid = resp->u.wifi_ap_get_sta_aid.aid;
	}

	return rpc_rsp_callback(resp);
}

int rpc_wifi_sta_get_rssi(int *rssi)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	if (!rssi)
		return FAILURE;

	resp = rpc_slaveif_wifi_sta_get_rssi(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*rssi = resp->u.wifi_sta_get_rssi.rssi;
	}

	return rpc_rsp_callback(resp);
}

int rpc_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap)
{
	/* implemented synchronous */
	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	req->u.wifi_protocol.ifx = ifx;
	req->u.wifi_protocol.protocol_bitmap = protocol_bitmap;

	resp = rpc_slaveif_wifi_set_protocol(req);
	return rpc_rsp_callback(resp);
}

int rpc_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap)
{
	/* implemented synchronous */
	if (!protocol_bitmap)
		return FAILURE;

	ctrl_cmd_t *req = RPC_DEFAULT_REQ();
	ctrl_cmd_t *resp = NULL;

	resp = rpc_slaveif_wifi_get_protocol(req);
	if (resp && resp->resp_event_status == SUCCESS) {
		*protocol_bitmap = resp->u.wifi_protocol.protocol_bitmap;
	}

	return rpc_rsp_callback(resp);
}
