#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "led.h"
#include "key.h"
#include "myiic.h"
#include "mipi_lcd.h"
#include "Camera.hpp"


extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();     /* 初始化NVS */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    led_init();                 /* LED初始化 */
    key_init();                 /* KEY初始化 */
    myiic_init();               /* MYIIC初始化 */
    lcd_init();                 /* LCD屏初始化 */
    
    lcd_show_string(30, 50,  200, 16, 16, "ESP32-P4", RED);
    lcd_show_string(30, 70,  200, 16, 16, "AI DEMO", RED);
    lcd_show_string(30, 90,  200, 16, 16, "ATOM@ALIENTEK", RED);
    lcd_show_string(30, 110, 200, 16, 16, "BOOT: switch mode", RED);

    mipi_dev_bsp_enable_dsi_phy_power();    /* 摄像头电源使能 */
    
    Camera *camera = new Camera(1280, 960);
    camera->init();
    camera->run();
}