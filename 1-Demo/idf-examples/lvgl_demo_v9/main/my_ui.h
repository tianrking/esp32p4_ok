// my_ui.h
#ifndef MY_UI_H
#define MY_UI_H

#include "lvgl.h"
#include <stdbool.h> // 确保bool类型可用

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化自定义用户界面
 * (前两个按钮通过MQTT控制，其他按钮为本地UI交互)
 * 此函数应在 LVGL、显示驱动和 MQTT 客户端准备好后调用。
 */
void my_ui_init(void);

/**
 * @brief 由 MQTT 事件处理器调用，当收到来自 Home Assistant 的状态更新时。
 * 这个函数会安全地更新 LVGL UI 上对应MQTT控制按钮的视觉状态。
 *
 * @param light_id 灯/继电器编号 (1 代表灯1/继电器1, 2 代表灯2/继电器2)
 * @param is_on true 表示状态为 ON, false 表示状态为 OFF
 */
void my_ui_update_light_status_from_mqtt(int light_id, bool is_on); // <<< 确保这个声明存在且正确

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*MY_UI_H*/