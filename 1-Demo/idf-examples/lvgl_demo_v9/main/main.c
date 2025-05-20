// main.c
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h> // For PRIu32 macro

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h" 

#include "mqtt_client.h" // ESP-MQTT客户端库

// LVGL and BSP includes
#include "lvgl.h"
#include "bsp/esp-bsp.h" // For display initialization

// Your custom UI
#include "my_ui.h"

// --- Configuration ---
// Wi-Fi
#define WIFI_SSID      "SEEED_Solution" // TODO: 替换为你的Wi-Fi SSID
#define WIFI_PASS      "chck1208"     // TODO: 替换为你的Wi-Fi密码
#define WIFI_MAXIMUM_RETRY  5

// MQTT
#define MQTT_BROKER_IP   "192.168.2.2" // TODO: 确认这是你的MQTT Broker IP
#define MQTT_BROKER_PORT 1883
// MQTT 用户名密码为空
#define MQTT_CLIENT_ID_BASE "s3w0x7ceswitch_" // Client ID基础部分
#define HA_DEVICE_ID_BASE   "s3w0x7ce_"       // Home Assistant Device ID 基础部分

#define MQTT_AVAILABILITY_TOPIC   "s3w0x7ceswitch/status"
#define MQTT_PAYLOAD_AVAILABLE    "online"
#define MQTT_PAYLOAD_NOT_AVAILABLE "offline"
#define MQTT_RELAY1_COMMAND_TOPIC "s3w0x7ceswitch/switch/relay1/command" // ESP32 UI 发布到此
#define MQTT_RELAY2_COMMAND_TOPIC "s3w0x7ceswitch/switch/relay2/command" // ESP32 UI 发布到此
#define MQTT_RELAY1_STATE_TOPIC   "s3w0x7ceswitch/switch/relay1/state"   // ESP32 UI 订阅此
#define MQTT_RELAY2_STATE_TOPIC   "s3w0x7ceswitch/switch/relay2/state"   // ESP32 UI 订阅此

// --- Global Variables & TAGs ---
static const char *TAG_MAIN = "APP_MAIN";
static const char *TAG_WIFI_CONNECT = "WIFI_CONNECT"; // 避免与官方示例的 TAG 冲突
static const char *TAG_MQTT_APP = "MQTT_APP";     // MQTT 应用特定的 TAG

// Wi-Fi Event Group
static EventGroupHandle_t s_wifi_event_group_main;
#define WIFI_CONNECTED_BIT_MAIN BIT0
#define WIFI_FAIL_BIT_MAIN      BIT1
static int s_wifi_retry_num_main = 0;

// MQTT Client Handle (全局，供 my_ui.c 使用)
esp_mqtt_client_handle_t client = NULL; // 这个定义是正确的
char mqtt_unique_client_id[64];
char ha_unique_device_id[64];


// --- ESP-IDF Official Example Style Error Logger ---
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT_APP, "Last error %s: 0x%x (%s)", message, error_code, esp_err_to_name(error_code));
    }
}

// --- MQTT Event Handler (定制化) ---
static void mqtt_event_handler_callback(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG_MQTT_APP, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client_cb = event->client; // client 变量已经是全局的了
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT_APP, "MQTT_EVENT_CONNECTED");
        // 发布设备上线状态
        msg_id = esp_mqtt_client_publish(client, MQTT_AVAILABILITY_TOPIC, MQTT_PAYLOAD_AVAILABLE, 0, 1, true);
        ESP_LOGI(TAG_MQTT_APP, "Sent AVAILABILITY '%s' publish, msg_id=%d", MQTT_PAYLOAD_AVAILABLE, msg_id);

        // 订阅状态主题 (UI需要根据这个更新按钮外观)
        msg_id = esp_mqtt_client_subscribe(client, MQTT_RELAY1_STATE_TOPIC, 0); // QoS 0 for states
        ESP_LOGI(TAG_MQTT_APP, "Subscribed to %s, msg_id=%d", MQTT_RELAY1_STATE_TOPIC, msg_id);
        msg_id = esp_mqtt_client_subscribe(client, MQTT_RELAY2_STATE_TOPIC, 0); // QoS 0 for states
        ESP_LOGI(TAG_MQTT_APP, "Subscribed to %s, msg_id=%d", MQTT_RELAY2_STATE_TOPIC, msg_id);
        
        // 注意：此时HA可能还没有发布状态，或者ESP32错过了retain的消息。
        // 一个好的做法是，HA应该在ESP32上线后，主动查询或发布一次当前状态。
        // 或者，ESP32在连接后，可以发布一个“请求状态更新”的消息到特定主题，让HA响应。
        // 目前，my_ui_init()会以默认状态（如OFF）显示按钮，等待MQTT_EVENT_DATA更新。
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT_APP, "MQTT_EVENT_DISCONNECTED");
        // LWT 会处理离线状态的发布
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT_APP, "MQTT_EVENT_SUBSCRIBED, msg_id=%d for topic: %.*s", event->msg_id, event->topic_len, event->topic);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_MQTT_APP, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_MQTT_APP, "MQTT_EVENT_PUBLISHED, msg_id=%d (This is an ACK for QoS1/2, or for any QoS if event->data is set)", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT_APP, "MQTT_EVENT_DATA received");
        ESP_LOGI(TAG_MQTT_APP, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG_MQTT_APP, "DATA=%.*s", event->data_len, event->data);

        bool received_state_is_on = (strncmp(event->data, "ON", event->data_len) == 0);

        // 根据收到的状态主题更新UI
        if (strncmp(event->topic, MQTT_RELAY1_STATE_TOPIC, event->topic_len) == 0) {
            my_ui_update_light_status_from_mqtt(1, received_state_is_on);
        } else if (strncmp(event->topic, MQTT_RELAY2_STATE_TOPIC, event->topic_len) == 0) {
            my_ui_update_light_status_from_mqtt(2, received_state_is_on);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_MQTT_APP, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG_MQTT_APP, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG_MQTT_APP, "Connection refused error code: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG_MQTT_APP, "Unknown MQTT error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG_MQTT_APP, "Other MQTT event id:%d", event->event_id);
        break;
    }
}

// --- MQTT Application Start Function ---
static void mqtt_app_start(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    snprintf(mqtt_unique_client_id, sizeof(mqtt_unique_client_id), "%s%02X%02X%02X%02X%02X%02X",
             MQTT_CLIENT_ID_BASE, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(ha_unique_device_id, sizeof(ha_unique_device_id), "%s%02X%02X%02X%02X%02X%02X",
             HA_DEVICE_ID_BASE, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG_MQTT_APP, "Generated MQTT Client ID: %s", mqtt_unique_client_id);
    ESP_LOGI(TAG_MQTT_APP, "Generated HA Device ID for HA identifiers: %s", ha_unique_device_id);

    char broker_uri[128];
    snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", MQTT_BROKER_IP, MQTT_BROKER_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.client_id = mqtt_unique_client_id,
        .session.last_will = {
            .topic = MQTT_AVAILABILITY_TOPIC,
            .msg = MQTT_PAYLOAD_NOT_AVAILABLE,
            .msg_len = strlen(MQTT_PAYLOAD_NOT_AVAILABLE),
            .qos = 1,
            .retain = true
        }
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT_APP, "Failed to init MQTT client");
        return;
    }
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler_callback, NULL);
    esp_err_t err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MQTT_APP, "Failed to start MQTT client: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_MQTT_APP, "MQTT client started successfully.");
    }
}

// --- Wi-Fi Event Handler ---
static void main_wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_WIFI_CONNECT, "WIFI_EVENT_STA_START: Initiating Wi-Fi connection...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num_main < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num_main++;
            ESP_LOGI(TAG_WIFI_CONNECT, "Retry to connect to the AP (%d/%d)", s_wifi_retry_num_main, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group_main, WIFI_FAIL_BIT_MAIN);
            ESP_LOGE(TAG_WIFI_CONNECT, "Failed to connect to the AP after %d retries.", WIFI_MAXIMUM_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI_CONNECT, "Got IP address:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num_main = 0;
        xEventGroupSetBits(s_wifi_event_group_main, WIFI_CONNECTED_BIT_MAIN);
    }
}

// --- Wi-Fi Initialization ---
static void main_wifi_init_and_connect(void) {
    s_wifi_event_group_main = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &main_wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &main_wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            // .ssid and .password are set directly
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.capable = true, .required = false},
        },
    };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI_CONNECT, "main_wifi_init_and_connect: Waiting for Wi-Fi connection...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group_main,
                                           WIFI_CONNECTED_BIT_MAIN | WIFI_FAIL_BIT_MAIN,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT_MAIN) {
        ESP_LOGI(TAG_WIFI_CONNECT, "Connected to AP SSID: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT_MAIN) {
        ESP_LOGE(TAG_WIFI_CONNECT, "Failed to connect to SSID: %s. Halting.", WIFI_SSID);
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); } 
    } else {
        ESP_LOGE(TAG_WIFI_CONNECT, "UNEXPECTED Wi-Fi EVENT");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
}

// --- Main Application ---
void app_main(void)
{
    ESP_LOGI(TAG_MAIN, "[APP] Startup..");
    ESP_LOGI(TAG_MAIN, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG_MAIN, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG_MAIN, ESP_LOG_DEBUG);
    esp_log_level_set(TAG_WIFI_CONNECT, ESP_LOG_DEBUG);
    esp_log_level_set(TAG_MQTT_APP, ESP_LOG_DEBUG);
    // esp_log_level_set("MY_UI_REMOTE", ESP_LOG_DEBUG); // TAG_UI in my_ui.c
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_MAIN, "NVS initialized.");

    // Initialize TCP/IP stack and default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG_MAIN, "TCP/IP Stack and Event Loop initialized.");

    // Connect to Wi-Fi (blocking call)
    main_wifi_init_and_connect(); 
    ESP_LOGI(TAG_MAIN, "[APP] Free memory after Wi-Fi connect: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Start MQTT client (after Wi-Fi is connected)
    mqtt_app_start();
    ESP_LOGI(TAG_MAIN, "[APP] Free memory after MQTT start: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Initialize Display and LVGL
    ESP_LOGI(TAG_MAIN, "Initializing display...");
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE, 
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true, 
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_backlight_on();
    ESP_LOGI(TAG_MAIN, "Display initialized.");
    ESP_LOGI(TAG_MAIN, "[APP] Free memory after display init: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Initialize your custom UI
    ESP_LOGI(TAG_MAIN, "Initializing UI...");
    bsp_display_lock(0);
    my_ui_init();
    bsp_display_unlock();
    ESP_LOGI(TAG_MAIN, "UI initialized.");
    ESP_LOGI(TAG_MAIN, "[APP] Final free memory: %" PRIu32 " bytes", esp_get_free_heap_size());

    ESP_LOGI(TAG_MAIN, "App setup complete. Main task will now idle or exit.");
    // If app_main returns, FreeRTOS scheduler continues running other tasks.
    // For some applications, you might have a while(1) loop here.
}