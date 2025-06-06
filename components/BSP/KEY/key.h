/**
 ****************************************************************************************************
 * @file        key.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       按键驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-P4 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#ifndef __KEY_H
#define __KEY_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚定义 */
#define BOOT_GPIO_PIN   GPIO_NUM_35

/*IO操作*/
#define BOOT            gpio_get_level(BOOT_GPIO_PIN)

/* 按键按下定义 */
#define BOOT_PRES       1       /* BOOT按键按下 */

/* 函数声明 */
void key_init(void);            /* 初始化按键 */
uint8_t key_scan(uint8_t mode); /* 按键扫描函数 */

#ifdef __cplusplus
}
#endif

#endif
