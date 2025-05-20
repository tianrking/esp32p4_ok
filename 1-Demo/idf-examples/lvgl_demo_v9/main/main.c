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
// #include "esp_mac.h" // 不再需要，除非my_ui需要MAC地址

// LVGL and BSP includes
#include "lvgl.h"
#include "bsp/esp-bsp.h" // For display initialization

// Your custom UI
#include "my_ui.h"

// --- Configuration ---
// Wi-Fi
#define WIFI_SSID      "SEEED_Solution"
#define WIFI_PASS      "chck1208"
#define WIFI_MAXIMUM_RETRY  5

// --- Global Variables & TAGs ---
static const char *TAG_MAIN = "APP_MAIN";
static const char *TAG_WIFI_CONNECT = "WIFI_CONNECT";

// Wi-Fi Event Group
static EventGroupHandle_t s_wifi_event_group_main;
#define WIFI_CONNECTED_BIT_MAIN BIT0
#define WIFI_FAIL_BIT_MAIN      BIT1
static int s_wifi_retry_num_main = 0;

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

    ESP_ERROR_CHECK(esp_netif_init()); // 初始化 TCP/IP 堆栈
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 初始化默认事件循环

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
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // 根据你的网络安全设置调整
            .pmf_cfg = {.capable = true, .required = false},
        },
    };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) -1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0'; // 确保空终止
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) -1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0'; // 确保空终止


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
        ESP_LOGE(TAG_WIFI_CONNECT, "Failed to connect to SSID: %s. Check credentials or network.", WIFI_SSID);
        // 可以选择在这里重启或进入错误处理流程，而不是无限循环
        // while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    } else {
        ESP_LOGE(TAG_WIFI_CONNECT, "UNEXPECTED Wi-Fi EVENT");
        // while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
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
    // esp_log_level_set("MY_UI", ESP_LOG_DEBUG); // 可以在 my_ui.c 中定义 TAG_UI

    // Initialize NVS (Needed for Wi-Fi calibration data)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_MAIN, "NVS initialized.");

    // Initialize networking stack and connect to Wi-Fi (blocking call)
    // esp_netif_init() 和 esp_event_loop_create_default() 已移至 main_wifi_init_and_connect
    main_wifi_init_and_connect();
    ESP_LOGI(TAG_MAIN, "[APP] Free memory after Wi-Fi connect: %" PRIu32 " bytes", esp_get_free_heap_size());


    // Initialize Display and LVGL
    ESP_LOGI(TAG_MAIN, "Initializing display...");
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true, // 根据你的硬件配置
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_backlight_on();
    ESP_LOGI(TAG_MAIN, "Display initialized.");
    ESP_LOGI(TAG_MAIN, "[APP] Free memory after display init: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Initialize your custom UI
    ESP_LOGI(TAG_MAIN, "Initializing UI...");
    bsp_display_lock(0); // 锁定显示缓冲区，防止LVGL任务和当前任务冲突
    my_ui_init();        // 调用你的UI初始化函数
    bsp_display_unlock(); // 解锁
    ESP_LOGI(TAG_MAIN, "UI initialized.");
    ESP_LOGI(TAG_MAIN, "[APP] Final free memory: %" PRIu32 " bytes", esp_get_free_heap_size());

    ESP_LOGI(TAG_MAIN, "App setup complete. Main task will now idle or exit if nothing else to do.");
    // FreeRTOS 会继续运行其他任务，例如 LVGL 的处理任务 bsp_display_task
}