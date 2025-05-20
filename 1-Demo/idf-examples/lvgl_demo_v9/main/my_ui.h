#ifndef MY_UI_H
#define MY_UI_H

#include "lvgl.h" // 必须包含LVGL核心头文件

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化自定义用户界面。
 *
 * 此函数应在 LVGL 和显示驱动程序初始化后调用。
 * 它设置主屏幕并尝试显示从 AA.c 文件加载的GIF动画。
 */
void my_ui_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*MY_UI_H*/
