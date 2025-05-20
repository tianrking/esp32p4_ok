#include "my_ui.h"
#include "lvgl.h"
#include "esp_log.h" // ESP-IDF 日志库

// -----------------------------------------------------------------------------
// 声明由 LVGL 在线转换器生成的图像资源 (在 AA.c 文件中定义)
// 这里的名称 "AA" 必须与您的 AA.c 文件中定义的 lv_image_dsc_t 变量名完全一致。
// 这个名称是您在 LVGL 在线转换器中为图片指定的 "Image name"。
// -----------------------------------------------------------------------------
LV_IMG_DECLARE(AA); // <--- 假设您在转换器中将图片命名为 "AA"
// -----------------------------------------------------------------------------

static const char *TAG_MY_UI = "MY_UI_STATIC_PNG";

void my_ui_init(void) {
    ESP_LOGI(TAG_MY_UI, "Initializing UI: Attempting to display static PNG from AA.c...");

    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        ESP_LOGE(TAG_MY_UI, "Failed to get active screen!");
        return;
    }

    // 创建一个图像对象 (lv_image)
    lv_obj_t *png_image = lv_image_create(scr);
    if (!png_image) {
        ESP_LOGE(TAG_MY_UI, "Failed to create LVGL image object!");
        return;
    }

    // 设置图像对象的来源为 AA.c 中定义的图像描述符
    // 我们使用 &AA 来获取指向 lv_image_dsc_t 结构体 AA 的指针。
    ESP_LOGI(TAG_MY_UI, "Setting image source to pre-converted C array: AA");
    lv_image_set_src(png_image, &AA); // <--- 使用在 AA.c 中定义的资源

    // 将图像居中显示在屏幕上
    // 您可以根据您的屏幕尺寸 (800x1280) 和PNG图像尺寸进行调整
    // 例如，如果想让图像在屏幕中水平居中，垂直方向靠上一些：
    // lv_obj_align(png_image, LV_ALIGN_TOP_MID, 0, 50); // 顶部中间，向下偏移50像素
    
    lv_obj_center(png_image); // 默认居中

    // (可选) 如果图像有透明度并且转换时保留了Alpha通道 (例如选择了 ARGB8888 格式),
    // 您可能需要启用对象的 alpha blending (通常默认是启用的，但可以明确设置)
    // lv_obj_set_style_image_opa(png_image, LV_OPA_COVER, 0);


    ESP_LOGI(TAG_MY_UI, "Image object created and source set from AA.c. PNG should be displayed.");

    // 注意：与GIF不同，lv_image 对象本身不处理 LV_USE_GIF 宏。
    // 它依赖于LVGL核心的图像解码能力来处理 AA.c 中描述的像素格式。
}
