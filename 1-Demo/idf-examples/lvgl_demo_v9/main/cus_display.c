/*
 * SPDX-FileCopyrightText: 2025 Your Name/Company
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include "cus_display.h" // 包含我们创建的头文件
 #include "lvgl.h"
 #include "esp_log.h"
 
 static const char *TAG_CUS = "cus_display";
 
 // 动画执行回调函数，用于设置对象的 X 坐标
 static void anim_x_cb(void *var, int32_t v)
 {
     if (var) { // 确保 var 不是 NULL
         lv_obj_set_x((lv_obj_t *)var, v);
     }
 }
 
 // 初始化自定义界面的函数
 void cus_display_init(void)
 {
     lv_obj_t *scr = lv_scr_act(); // 获取当前活动屏幕
     if (!scr) {
         ESP_LOGE(TAG_CUS, "错误：找不到活动屏幕！");
         return;
     }
 
     // 1. 创建一个简单的矩形对象作为动画目标
     lv_obj_t *animated_rect = lv_obj_create(scr);
     lv_obj_set_size(animated_rect, 80, 50); // 设置矩形大小 (宽度, 高度)
     // 初始对齐到屏幕左侧中间，并向右偏移10像素
     lv_obj_align(animated_rect, LV_ALIGN_LEFT_MID, 10, 0);
     // 设置背景颜色为蓝色系主色
     lv_obj_set_style_bg_color(animated_rect, lv_palette_main(LV_PALETTE_BLUE), 0);
     // 设置圆角
     lv_obj_set_style_radius(animated_rect, 5, 0);
 
     // 获取屏幕宽度，使动画更具响应性
     lv_coord_t screen_width = lv_disp_get_hor_res(lv_disp_get_default());
     lv_coord_t rect_width = lv_obj_get_width(animated_rect);
 
     // 2. 配置水平移动动画
     lv_anim_t a_x;
     lv_anim_init(&a_x);
     lv_anim_set_var(&a_x, animated_rect); // 设置动画变量为矩形对象
     // 设置动画的起始值和结束值 (从x=10 到 x=屏幕宽度-矩形宽度-10)
     lv_anim_set_values(&a_x, 10, screen_width - rect_width - 10);
     lv_anim_set_time(&a_x, 2000); // 动画时长 (2000毫秒 = 2秒)
     lv_anim_set_playback_time(&a_x, 2000); // 回放时长 (2秒，实现来回效果)
     lv_anim_set_repeat_count(&a_x, LV_ANIM_REPEAT_INFINITE); // 无限重复
     lv_anim_set_path_cb(&a_x, lv_anim_path_ease_in_out); // 设置动画路径（缓入缓出）
     lv_anim_set_exec_cb(&a_x, anim_x_cb); // 设置执行回调函数
     lv_anim_start(&a_x); // 启动动画
 
     // (可选) 添加一个标签
     lv_obj_t *label = lv_label_create(scr);
     lv_label_set_text(label, "自定义动画演示!");
     lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20); // 对齐到屏幕顶部中间
 
     ESP_LOGI(TAG_CUS, "自定义界面已初始化并启动动画。");
 }