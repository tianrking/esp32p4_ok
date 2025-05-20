/*
 * Espressif Systems Wireless LAN device driver
 *
 * Copyright (C) 2015-2022 Espressif Systems (Shanghai) PTE LTD
 * SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
 */
#include "rpc_slave_if.h"
#include "rpc_core.h"
#include "esp_hosted_wifi_config.h"
#include "esp_log.h"

DEFINE_LOG_TAG(rpc_api);

#define RPC_SEND_REQ(msGiD) do {                                                \
    assert(req);                                                                \
    req->msg_id = msGiD;                                                        \
    if(SUCCESS != rpc_send_req(req)) {                                          \
        ESP_LOGE(TAG,"Failed to send control req 0x%x\n", req->msg_id);         \
        return NULL;                                                            \
    }                                                                           \
} while(0);

#define RPC_DECODE_RSP_IF_NOT_ASYNC() do {                                      \
  if (req->rpc_rsp_cb)                                                          \
    return NULL;                                                                \
  return rpc_wait_and_parse_sync_resp(req);                                     \
} while(0);


int rpc_slaveif_init(void)
{
	ESP_LOGD(TAG, "%s", __func__);
	return rpc_core_init();
}

int rpc_slaveif_deinit(void)
{
	ESP_LOGD(TAG, "%s", __func__);
	return rpc_core_deinit();
}

/** Control Req->Resp APIs **/
ctrl_cmd_t * rpc_slaveif_wifi_get_mac(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_GetMACAddress);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_mac(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_SetMacAddress);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_mode(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_GetWifiMode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_mode(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_SetWifiMode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_ps(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetPs);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_ps(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetPs);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_max_tx_power(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetMaxTxPower);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_max_tx_power(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetMaxTxPower);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_config_heartbeat(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_ConfigHeartbeat);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_ota_begin(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_OTABegin);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_ota_write(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_OTAWrite);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_ota_end(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_OTAEnd);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_init(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiInit);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_deinit(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiDeinit);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_start(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiStart);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_stop(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiStop);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_connect(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiConnect);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_disconnect(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiDisconnect);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_config(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetConfig);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_config(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetConfig);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_scan_start(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiScanStart);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_scan_stop(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiScanStop);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_scan_get_ap_num(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiScanGetApNum);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_scan_get_ap_record(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiScanGetApRecord);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_scan_get_ap_records(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiScanGetApRecords);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_clear_ap_list(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiClearApList);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_restore(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiRestore);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_clear_fast_connect(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiClearFastConnect);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_deauth_sta(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiDeauthSta);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_sta_get_ap_info(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiStaGetApInfo);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_storage(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetStorage);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_bandwidth(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetBandwidth);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_bandwidth(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetBandwidth);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_channel(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetChannel);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_channel(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetChannel);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_country_code(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetCountryCode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_country_code(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetCountryCode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_country(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetCountry);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_country(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetCountry);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_ap_get_sta_list(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiApGetStaList);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_ap_get_sta_aid(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiApGetStaAid);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_sta_get_rssi(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiStaGetRssi);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_protocol(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetProtocol);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_protocol(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetProtocol);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_sta_get_negotiated_phymode(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiStaGetNegotiatedPhymode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_sta_get_aid(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiStaGetAid);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_protocols(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetProtocols);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_get_coprocessor_fwversion(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_GetCoprocessorFwVersion);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

#if H_WIFI_DUALBAND_SUPPORT
ctrl_cmd_t * rpc_slaveif_wifi_get_protocols(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetProtocols);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_bandwidths(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetBandwidths);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_bandwidths(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetBandwidths);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_band(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetBand);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_band(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetBand);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_set_band_mode(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiSetBandMode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}

ctrl_cmd_t * rpc_slaveif_wifi_get_band_mode(ctrl_cmd_t *req)
{
	RPC_SEND_REQ(RPC_ID__Req_WifiGetBandMode);
	RPC_DECODE_RSP_IF_NOT_ASYNC();
}
#endif
