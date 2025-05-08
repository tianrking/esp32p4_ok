#ifndef CUS_DISPLAY_H
#define CUS_DISPLAY_H

// 如果 cus_display_init 函数的参数或返回值使用了 LVGL 特定的类型，
// 你可能需要在这里 #include "lvgl.h"。
// 但对于 void cus_display_init(void); 这样的声明，则不是必需的。

/**
 * @brief 初始化自定义 LVGL 显示元素和动画。
 *
 * 此函数在 LVGL 初始化并获得显示锁之后调用，
 * 用于创建自定义的界面和动画效果。
 */
void cus_display_init(void);

#endif // CUS_DISPLAY_H