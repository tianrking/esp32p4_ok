/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_video_init.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/ppa.h"

#include "app_video.h"
#include "app_pedestrian_detect.h"
#include "app_humanface_detect.h"
#include "app_camera_pipeline.hpp"
#include "Camera.hpp"

/* 自添加 */
#include "myiic.h"
#include "lcd.h"
#include "key.h"

// extern "C" void cv_text_detection(uint8_t *data, int width, int heigth);

// extern "C" void cv_print_info();

#define BSP_LCD_BITS_PER_PIXEL 16

#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

#define CAMERA_INIT_TASK_WAIT_MS (1000)
#define DETECT_NUM_MAX (10)

using namespace std;

typedef enum
{
    CAMERA_EVENT_TASK_RUN = BIT(0),
    CAMERA_EVENT_DELETE = BIT(1),
    CAMERA_EVENT_PED_DETECT = BIT(2),
    CAMERA_EVENT_HUMAN_DETECT = BIT(3),
    CAMERA_EVENT_TEXT_DETECT = BIT(4), // <--- 新增文本检测事件
} camera_event_id_t;

static const char *TAG = "Camera";

static void **detect_buf;
static vector<vector<int>> detect_bound;
static vector<vector<int>> detect_keypoints;
static std::list<dl::detect::result_t> detect_results;
static PedestrianDetect **ped_detect = NULL;
static HumanFaceDetect **hum_detect = NULL;

static pipeline_handle_t feed_pipeline;
static pipeline_handle_t detect_pipeline;

// Other variables
static size_t data_cache_line_size = 0;
static ppa_client_handle_t ppa_client_srm_handle = NULL;
static ppa_client_handle_t ppa_deal_client_srm_handle = NULL;
static EventGroupHandle_t camera_event_group;

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);
static bool ppa_trans_done_cb(ppa_client_handle_t ppa_client, ppa_event_data_t *event_data, void *user_data);

void *draw_buffer;
void *temp_buffer0;
void *temp_buffer1;

static size_t lcd_data_cache_line_size = 0;

Camera::Camera(uint16_t hor_res, uint16_t ver_res) : _hor_res(hor_res),
                                                     _ver_res(ver_res),
                                                     _camera_init_sem(NULL),
                                                     _camera_ctlr_handle(0)
{
}

Camera::~Camera()
{
}

bool Camera::run(void)
{

    // cv_print_info();

    uint8_t key_value = 0;
    uint8_t work_mode = 3;

    /* camera init */
    ESP_ERROR_CHECK(app_video_set_bufs(_camera_ctlr_handle, EXAMPLE_CAM_BUF_NUM, (const void **)_cam_buffer));
    ESP_LOGI(TAG, "Start camera stream task");
    ESP_ERROR_CHECK(app_video_stream_task_start(_camera_ctlr_handle, 0));

    ped_detect = get_pedestrian_detect();
    *ped_detect = new PedestrianDetect();
    assert(*ped_detect != NULL);

    hum_detect = get_humanface_detect();
    *hum_detect = new HumanFaceDetect();
    assert(*hum_detect != NULL);

    xTaskCreatePinnedToCore((TaskFunction_t)camera_dectect_task, "Camera Detect", 1024 * 8, this, 5, &_detect_task_handle, 1);

    xEventGroupSetBits(camera_event_group, CAMERA_EVENT_TASK_RUN);
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_DELETE);

    lcd_clear(BLACK);

    if (lcddev.id > 0x7084) /* MIPI屏幕才有显示 */
    {
        lcd_show_string((lcddev.width - 13 * 16) / 2, ((lcddev.height - 640) / 2 / 2) - 16, 300, 32, 32, "normal_detect", WHITE);
    }
    else if (lcddev.id == 0x7016)
    {
        lcd_show_string((lcddev.width - 13 * 16) / 2, ((lcddev.height - 480) / 2 / 2) - 16, 300, 32, 32, "normal_detect", WHITE);
    }

    while (1)
    {
        key_value = key_scan(0);
        if (key_value)
        {
            work_mode++; // 先递增工作模式

            if (work_mode == 1) // 模式1: 行人检测
            {
                lcd_clear(BLACK);
                // 清除其他AI检测标志, 设置行人检测标志
                xEventGroupClearBits(camera_event_group, CAMERA_EVENT_HUMAN_DETECT | CAMERA_EVENT_TEXT_DETECT);
                xEventGroupSetBits(camera_event_group, CAMERA_EVENT_PED_DETECT);

                if (lcddev.id > 0x7084)
                {
                    lcd_show_string((lcddev.width - 17 * 16) / 2, ((lcddev.height - 640) / 2 / 2) - 16, 300, 32, 32, "pedestrian_detect", WHITE);
                }
                else if (lcddev.id == 0x7016)
                {
                    lcd_show_string((lcddev.width - 17 * 16) / 2, ((lcddev.height - 480) / 2 / 2) - 16, 300, 32, 32, "pedestrian_detect", WHITE);
                }
            }
            else if (work_mode == 2) // 模式2: 人脸检测
            {
                lcd_clear(BLACK);
                // 清除其他AI检测标志, 设置人脸检测标志
                xEventGroupClearBits(camera_event_group, CAMERA_EVENT_PED_DETECT | CAMERA_EVENT_TEXT_DETECT);
                xEventGroupSetBits(camera_event_group, CAMERA_EVENT_HUMAN_DETECT);

                if (lcddev.id > 0x7084)
                {
                    lcd_show_string((lcddev.width - 16 * 16) / 2, ((lcddev.height - 640) / 2 / 2) - 16, 300, 32, 32, "humanface_detect", WHITE);
                }
                else if (lcddev.id == 0x7016)
                {
                    lcd_show_string((lcddev.width - 16 * 16) / 2, ((lcddev.height - 480) / 2 / 2) - 16, 300, 32, 32, "humanface_detect", WHITE);
                }
            }
            else if (work_mode == 3) // 模式3: 文本检测
            {
                lcd_clear(BLACK);
                // 清除其他AI检测标志, 设置文本检测标志
                xEventGroupClearBits(camera_event_group, CAMERA_EVENT_PED_DETECT | CAMERA_EVENT_HUMAN_DETECT);
                xEventGroupSetBits(camera_event_group, CAMERA_EVENT_TEXT_DETECT);

                // 假设 "text_detect" 字符串长度为 11 (可以根据实际调整下面的 11*16)
                if (lcddev.id > 0x7084)
                {
                    lcd_show_string((lcddev.width - 11 * 16) / 2, ((lcddev.height - 640) / 2 / 2) - 16, 300, 32, 32, "circle_detect", WHITE);
                }
                else if (lcddev.id == 0x7016)
                {
                    lcd_show_string((lcddev.width - 11 * 16) / 2, ((lcddev.height - 480) / 2 / 2) - 16, 300, 32, 32, "circle_detect", WHITE);
                }
            }
            else if (work_mode >= 4) // 如果 work_mode 达到4 (或更大，以防万一), 则循环回模式0 (普通模式)
            {
                lcd_clear(BLACK);
                work_mode = 0; // 重置为普通模式
                // 清除所有AI检测标志
                xEventGroupClearBits(camera_event_group, CAMERA_EVENT_PED_DETECT | CAMERA_EVENT_HUMAN_DETECT | CAMERA_EVENT_TEXT_DETECT);

                if (lcddev.id > 0x7084)
                {
                    lcd_show_string((lcddev.width - 13 * 16) / 2, ((lcddev.height - 640) / 2 / 2) - 16, 300, 32, 32, "normal_detect", WHITE);
                }
                else if (lcddev.id == 0x7016)
                {
                    lcd_show_string((lcddev.width - 13 * 16) / 2, ((lcddev.height - 480) / 2 / 2) - 16, 300, 32, 32, "normal_detect", WHITE);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // pdMS_TO_TICKS 是更推荐的写法
    }

    return true;
}

bool Camera::init(void)
{
    /* 获取LCD显示屏的buffer帧缓存区 */
    if (lcddev.id <= 0x7084)
    {
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(lcddev.lcd_panel_handle, 2, &lcddev_buffer[0], &lcddev_buffer[1]));
    }
    else
    {
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(lcddev.lcd_panel_handle, 2, &lcddev_buffer[0], &lcddev_buffer[1]));
    }

    temp_buffer0 = lcddev_buffer[0];
    temp_buffer1 = lcddev_buffer[1];

    camera_event_group = xEventGroupCreate();
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_TASK_RUN);
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_DELETE);
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_PED_DETECT);
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_HUMAN_DETECT);
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_TEXT_DETECT);

    esp_err_t ret = app_video_main(bus_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "video main init failed with error 0x%x", ret);
    }

    _camera_ctlr_handle = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT_RGB565);
    if (_camera_ctlr_handle < 0)
    {
        ESP_LOGE(TAG, "video cam open failed");
    }

    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &lcd_data_cache_line_size));

    /* 申请给摄像头的两个buffer */
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++)
    {
        _cam_buffer[i] = (uint8_t *)heap_caps_aligned_alloc(data_cache_line_size, _hor_res * _ver_res * BSP_LCD_BITS_PER_PIXEL / 8, MALLOC_CAP_SPIRAM);
        _cam_buffer_size[i] = _hor_res * _ver_res * BSP_LCD_BITS_PER_PIXEL / 8;

        if (NULL != _cam_buffer[i])
        {
            ESP_LOGI("buffer init", "_cam_buffer malloc succeed");
        }
    }

    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    /* 专门用于AI处理 */
    size_t detect_buf_size = ALIGN_UP_BY(_hor_res * _ver_res * BSP_LCD_BITS_PER_PIXEL / 8, data_cache_line_size);
    detect_buf = (void **)malloc(sizeof(void *));
    if (detect_buf)
    {
        *detect_buf = (uint16_t *)heap_caps_aligned_calloc(data_cache_line_size, 1, detect_buf_size, MALLOC_CAP_SPIRAM);
    }

    ppa_client_config_t srm_config =
        {
            .oper_type = PPA_OPERATION_SRM,
        };
    ESP_ERROR_CHECK(ppa_register_client(&srm_config, &ppa_client_srm_handle));

    ppa_event_callbacks_t cbs = /* PPA回调函数 */
        {
            .on_trans_done = ppa_trans_done_cb,
        };
    ppa_client_register_event_callbacks(ppa_client_srm_handle, &cbs);

    /*---------------用作AI处理后的图像刷屏------------------*/
    ppa_client_config_t ppa_deal_srm_config = {
        /* 设置客户端的属性 */
        .oper_type = PPA_OPERATION_SRM, /* PPA操作类型(已注册的PPA客户只能请求一种) */
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_deal_srm_config, &ppa_deal_client_srm_handle)); /* 注册PPA客户端 */

    camera_pipeline_cfg_t PPA_feed_cfg = /* 用于检测的通道 */
        {
            .elem_num = 1,
            .elements = detect_buf,
            .align_size = 1,
            .caps = MALLOC_CAP_SPIRAM,
            .buffer_size = detect_buf_size,
        };
    camera_element_pipeline_new(&PPA_feed_cfg, &feed_pipeline);

    camera_pipeline_cfg_t detect_feed_cfg =
        {
            .elem_num = 1,
            .elements = NULL,
            .align_size = 1,
            .caps = MALLOC_CAP_SPIRAM,
            .buffer_size = 20 * sizeof(int),
        };
    camera_element_pipeline_new(&detect_feed_cfg, &detect_pipeline);

    return true;
}

static bool ppa_trans_done_cb(ppa_client_handle_t ppa_client, ppa_event_data_t *event_data, void *user_data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    camera_pipeline_buffer_element *p = (camera_pipeline_buffer_element *)user_data;
    camera_pipeline_done_element(feed_pipeline, p);

    return (xHigherPriorityTaskWoken == pdTRUE);
}

/* 不要执行任何刷屏操作 只负责检测，返回检测结果 */
void Camera::camera_dectect_task(Camera *app)
{
    while (1)
    {
        xEventGroupWaitBits(camera_event_group, CAMERA_EVENT_TASK_RUN, pdFALSE, pdTRUE, portMAX_DELAY);

        if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_PED_DETECT)
        {
            /* 从feed_pipeline中把detect buffer传递给app_pedestrian_detect处理 */
            camera_pipeline_buffer_element *p = camera_pipeline_recv_element(feed_pipeline, portMAX_DELAY);
            if (p)
            {
                if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_PED_DETECT)
                {
                    detect_results = app_pedestrian_detect((uint16_t *)p->buffer, app->_ver_res, app->_hor_res);
                }

                camera_pipeline_queue_element_index(feed_pipeline, p->index);

                camera_pipeline_buffer_element *element = camera_pipeline_get_queued_element(detect_pipeline);
                if (element)
                {
                    element->detect_results = &detect_results;

                    camera_pipeline_done_element(detect_pipeline, element);
                }
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(80));
        }

        if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_DELETE)
        {
            if (*ped_detect)
            {
                delete *ped_detect;
            }

            ESP_LOGI(TAG, "Camera detect task exit");
            vTaskDelete(NULL);
        }
    }
}

/* 负责发送检查的 buffer，然后接受结果处理在 camera buffer 上，然后再执行刷屏操作 */
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    draw_buffer = temp_buffer1 == draw_buffer ? temp_buffer0 : temp_buffer1; /* 双缓冲情况下,切换buffer */

    xEventGroupWaitBits(camera_event_group, CAMERA_EVENT_TASK_RUN, pdFALSE, pdTRUE, portMAX_DELAY);

    auto process_results = [&](const auto &results, bool process_keypoints)
    {
        detect_keypoints.clear();
        detect_bound.clear();
        for (const auto &res : results)
        {
            const auto &box = res.box;
            if (box.size() >= 4 && std::any_of(box.begin(), box.end(), [](int v)
                                               { return v != 0; }))
            {
                detect_bound.push_back(std::move(box));

                if (process_keypoints && res.keypoint.size() >= 10 &&
                    std::any_of(res.keypoint.begin(), res.keypoint.end(), [](int v)
                                { return v != 0; }))
                {
                    detect_keypoints.push_back(std::move(res.keypoint));
                }
            }
        }
    };

    if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_PED_DETECT)
    {
        /* 将摄像头数据绑定到detect buffer中，在camera_dectect_task中处理 */
        camera_pipeline_buffer_element *p = camera_pipeline_get_queued_element(feed_pipeline);
        if (p)
        {
            ppa_srm_oper_config_t oper_config;
            oper_config.in.buffer = (void *)camera_buf;
            oper_config.in.pic_w = camera_buf_hes;
            oper_config.in.pic_h = camera_buf_ves;
            oper_config.in.block_w = camera_buf_hes;
            oper_config.in.block_h = camera_buf_ves;
            oper_config.in.block_offset_x = 0;
            oper_config.in.block_offset_y = 0;
            oper_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

            oper_config.out.buffer = p->buffer;
            oper_config.out.buffer_size = ALIGN_UP_BY(camera_buf_hes * camera_buf_ves * BSP_LCD_BITS_PER_PIXEL / 8, data_cache_line_size);
            oper_config.out.pic_w = camera_buf_hes;
            oper_config.out.pic_h = camera_buf_ves;
            oper_config.out.block_offset_x = 0;
            oper_config.out.block_offset_y = 0;
            oper_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

            oper_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
            oper_config.scale_x = 1;
            oper_config.scale_y = 1;
            oper_config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;

            oper_config.rgb_swap = 0;
            oper_config.byte_swap = 0;

            oper_config.user_data = (void *)p,
            oper_config.mode = PPA_TRANS_MODE_NON_BLOCKING;

            int res = ppa_do_scale_rotate_mirror(ppa_client_srm_handle, &oper_config);
            if (res != ESP_OK)
            {
                ESP_LOGE(TAG, "ppa_do_scale_rotate_mirror 1 failed with error 0x%x", res);
            }
        }

        camera_pipeline_buffer_element *detect_element = camera_pipeline_recv_element(detect_pipeline, 0);
        if (detect_element)
        {
            process_results(*(detect_element->detect_results), false);

            camera_pipeline_queue_element_index(detect_pipeline, detect_element->index);
        }

        for (int i = 0; i < detect_bound.size(); i++)
        {
            if (detect_bound[i].size() >= 4 && std::any_of(detect_bound[i].begin(), detect_bound[i].end(), [](int v)
                                                           { return v != 0; }))
            {
                draw_rectangle_rgb((uint16_t *)camera_buf, camera_buf_hes, camera_buf_ves,
                                   detect_bound[i][0], detect_bound[i][1], detect_bound[i][2], detect_bound[i][3],
                                   0, 0, 255, 0, 0, 3);
            }
        }
    }
    else if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_HUMAN_DETECT)
    {

        detect_results = app_humanface_detect((uint16_t *)camera_buf, camera_buf_ves, camera_buf_hes);

        process_results(detect_results, true);

        for (int i = 0; i < detect_keypoints.size(); i++)
        {

            if (detect_bound[i].size() >= 4 && std::any_of(detect_bound[i].begin(), detect_bound[i].end(), [](int v)
                                                           { return v != 0; }))
            {
                draw_rectangle_rgb((uint16_t *)camera_buf, camera_buf_hes, camera_buf_ves,
                                   detect_bound[i][0], detect_bound[i][1], detect_bound[i][2], detect_bound[i][3],
                                   0, 0, 255, 0, 0, 3);

                if (detect_keypoints[i].size() >= 10)
                {
                    draw_green_points((uint16_t *)camera_buf, detect_keypoints[i]);
                }
            }
        }
    }

    // else if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_TEXT_DETECT)
    // {
    //     ESP_LOGD(TAG, "Text detection mode active for current frame.");

    //     // --- 开始修改：处理ROI ---
    //     int full_width = camera_buf_hes;
    //     int full_height = camera_buf_ves;

    //     // 定义您希望处理的ROI的大小和位置
    //     // 例如，取图像中心的一个 320x240 的区域
    //     // 您可以根据需要调整这些值
    //     int roi_width = 320;
    //     int roi_height = 240;

    //     // 确保ROI尺寸不超过原始图像尺寸
    //     if (roi_width > full_width) roi_width = full_width;
    //     if (roi_height > full_height) roi_height = full_height;

    //     int roi_x = (full_width - roi_width) / 2;   // ROI左上角x坐标
    //     int roi_y = (full_height - roi_height) / 2; // ROI左上角y坐标

    //     ESP_LOGI(TAG, "Processing ROI for text detection: x=%d, y=%d, w=%d, h=%d", roi_x, roi_y, roi_width, roi_height);

    //     // // 1. 为完整的 camera_buf 创建一个临时的Mat头 (RGB565)
    //     cv::Mat full_rgb565_frame(full_height, full_width, CV_16UC1, camera_buf);

    //     // // 2. 从完整图像中提取ROI (这通常不复制数据，只是创建一个新的Mat头指向子区域)
    //     cv::Mat rgb565_roi_frame = full_rgb565_frame(cv::Rect(roi_x, roi_y, roi_width, roi_height));

    //     // // 3. 创建用于颜色转换的Mat对象
    //     cv::Mat bgr_roi_frame;  // 用于存放 BGR888 格式的ROI
    //     cv::Mat gray_roi_frame; // 用于存放灰度格式的ROI

    //     // // 4. 颜色转换：仅对ROI进行操作
    //     // // 如果 cvtColor 在处理非连续的Mat时有问题，可能需要先 clone ROI
    //     // // cv::Mat continuous_rgb565_roi = rgb565_roi_frame.clone(); // 如果需要，取消注释
    //     // // cv::cvtColor(continuous_rgb565_roi, bgr_roi_frame, cv::COLOR_BGR5652BGR);
    //     // cv::cvtColor(rgb565_roi_frame, bgr_roi_frame, cv::COLOR_BGR5652BGR); // 先尝试不clone
    //     // cv::cvtColor(bgr_roi_frame, gray_roi_frame, cv::COLOR_BGR2GRAY);

    //     // if (gray_roi_frame.empty()) {
    //     //     ESP_LOGE(TAG, "Failed to convert ROI to grayscale for text detection.");
    //     // } else {
    //     //     ESP_LOGI(TAG, "ROI converted to grayscale. Width: %d, Height: %d, Channels: %d",
    //     //              gray_roi_frame.cols, gray_roi_frame.rows, gray_roi_frame.channels());

    //     //     // 确保 gray_roi_frame.data 是连续的，或者克隆它
    //     //     // findContours 等函数通常期望 Mat 数据是连续的
    //     //     cv::Mat final_gray_roi_for_detection;
    //     //     if (gray_roi_frame.isContinuous()) {
    //     //         final_gray_roi_for_detection = gray_roi_frame;
    //     //     } else {
    //     //         ESP_LOGW(TAG, "Gray ROI frame is not continuous. Cloning for text detection.");
    //     //         final_gray_roi_for_detection = gray_roi_frame.clone();
    //     //     }

    //     //     // 5. 调用文本检测函数，传入ROI的数据和尺寸
    //     //     cv_text_detection(final_gray_roi_for_detection.data,
    //     //                       final_gray_roi_for_detection.cols,
    //     //                       final_gray_roi_for_detection.rows);
    //     //     // 注意: cv_text_detection 内部打印的坐标将是相对于这个ROI的。
    //     // }
    //     // --- ROI处理结束 ---
    // }

    // else if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_TEXT_DETECT) // 仍使用此标志位
    // {
    //     ESP_LOGI(TAG, "SIMPLE OPENCV TEST: BITWISE_NOT ON ROI");

    //     int full_width = camera_buf_hes;
    //     int full_height = camera_buf_ves;

    //     // --- 定义和提取ROI ---
    //     int roi_width = 320;
    //     int roi_height = 240;
    //     if (roi_width > full_width)
    //         roi_width = full_width;
    //     if (roi_height > full_height)
    //         roi_height = full_height;
    //     int roi_x = (full_width - roi_width) / 2;
    //     int roi_y = (full_height - roi_height) / 2;

    //     ESP_LOGI(TAG, "Processing ROI: x=%d, y=%d, w=%d, h=%d", roi_x, roi_y, roi_width, roi_height);

    //     // 1. 创建指向 camera_buf 中 ROI 区域的 Mat 头
    //     cv::Mat full_rgb565_input_header(full_height, full_width, CV_16UC1, camera_buf);
    //     if (full_rgb565_input_header.empty())
    //     {
    //         ESP_LOGE(TAG, "Full_rgb565_input_header is empty!");
    //         return;
    //     }
    //     cv::Mat roi_target_header = full_rgb565_input_header(cv::Rect(roi_x, roi_y, roi_width, roi_height));
    //     if (roi_target_header.empty())
    //     {
    //         ESP_LOGE(TAG, "roi_target_header is empty!");
    //         return;
    //     }

    //     // 2. 克隆ROI数据到新的Mat (rgb565_roi_copy) 以进行处理
    //     // 这是为了确保我们操作的是一份独立且可能连续的数据
    //     cv::Mat rgb565_roi_copy = roi_target_header.clone();
    //     if (rgb565_roi_copy.empty())
    //     {
    //         ESP_LOGE(TAG, "rgb565_roi_copy is empty after clone!");
    //         return;
    //     }
    //     ESP_LOGD(TAG, "Cloned RGB565 ROI: rows=%d, cols=%d, type=0x%X",
    //              rgb565_roi_copy.rows, rgb565_roi_copy.cols, rgb565_roi_copy.type());

    //     // 3. 执行一个简单的OpenCV操作：位反转 (bitwise_not)
    //     // 这个操作可以直接在 CV_16UC1 上进行
    //     cv::Mat inverted_roi;
    //     ESP_LOGD(TAG, "Applying bitwise_not to cloned ROI...");
    //     try
    //     {
    //         cv::bitwise_not(rgb565_roi_copy, inverted_roi);
    //     }
    //     catch (const cv::Exception &ex)
    //     {
    //         ESP_LOGE(TAG, "OpenCV exception during bitwise_not: %s", ex.what());
    //         return;
    //     }

    //     if (inverted_roi.empty())
    //     {
    //         ESP_LOGE(TAG, "Bitwise_not resulted in an empty image.");
    //         return;
    //     }
    //     ESP_LOGI(TAG, "Bitwise_not operation complete.");

    //     // 4. 将处理后的结果 (inverted_roi) 复制回 camera_buf 中的ROI区域
    //     ESP_LOGD(TAG, "Copying inverted ROI back to camera_buf's ROI region...");
    //     if (inverted_roi.size() == roi_target_header.size() &&
    //         inverted_roi.type() == roi_target_header.type())
    //     {
    //         inverted_roi.copyTo(roi_target_header); // Mat::copyTo 是安全的
    //         ESP_LOGI(TAG, "Inverted ROI successfully copied back to camera_buf.");
    //     }
    //     else
    //     {
    //         ESP_LOGE(TAG, "Output dimension/type mismatch for final copy after bitwise_not.");
    //     }

    //     // --- OpenCV简单效果测试结束 ---
    //     // 后续的PPA操作会将 camera_buf (包含修改后的ROI) 显示到LCD
    // }

    // else if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_TEXT_DETECT)
    // {
    //     // --- 1. 缓存同步 ---
    //     esp_cache_msync((void *)camera_buf, camera_buf_len, ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    //     // --- 2. 定义ROI ---
    //     int full_width = camera_buf_hes;
    //     int full_height = camera_buf_ves;
    //     int roi_width = 128;
    //     int roi_height = 128;
    //     if (roi_width > full_width)
    //         roi_width = full_width;
    //     if (roi_height > full_height)
    //         roi_height = full_height;
    //     int roi_x = (full_width - roi_width) / 2;
    //     int roi_y = (full_height - roi_height) / 2;

    //     // --- 3. 创建ROI视图 ---
    //     cv::Mat full_input_header(full_height, full_width, CV_8UC2, camera_buf);
    //     cv::Mat roi_to_process = full_input_header(cv::Rect(roi_x, roi_y, roi_width, roi_height));

    //     if (roi_to_process.empty())
    //     {
    //         return;
    //     }

    //     // --- 4. 使用static Mat ---
    //     static cv::Mat roi_snapshot;
    //     static cv::Mat gray_roi;
    //     static std::vector<cv::Vec3f> circles;

    //     try
    //     {
    //         // --- 5. 捕获快照并进行检测 ---
    //         roi_to_process.copyTo(roi_snapshot);
    //         cv::cvtColor(roi_snapshot, gray_roi, cv::COLOR_BGR5652GRAY);
    //         cv::GaussianBlur(gray_roi, gray_roi, cv::Size(5, 5), 2, 2);
    //         cv::HoughCircles(
    //             gray_roi, circles, cv::HOUGH_GRADIENT, 1,
    //             gray_roi.rows / 8, 80, 20, 5, 60);

    //         // --- 6. 关键逻辑修正：先用干净的快照覆盖显示区，清除一切旧标记 ---
    //         roi_snapshot.copyTo(roi_to_process);

    //         // --- 7. 在刚恢复干净的显示区上，按需绘制当前帧的内容 ---

    //         // 步骤A: 无论如何都绘制ROI的边界框
    //         cv::rectangle(roi_to_process, cv::Point(0, 0), cv::Point(roi_width - 1, roi_height - 1), cv::Scalar(0xFFE0), 1); // 黄色边框

    //         // 步骤B: 只在当前帧检测到圆时，才绘制圆
    //         if (!circles.empty())
    //         {
    //             cv::Scalar circle_color(0x07E0); // 纯绿色
    //             cv::Scalar center_color(0xF800); // 纯红色

    //             for (size_t i = 0; i < circles.size(); i++)
    //             {
    //                 cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
    //                 int radius = cvRound(circles[i][2]);
    //                 cv::circle(roi_to_process, center, radius, circle_color, 2, cv::LINE_AA);
    //                 cv::circle(roi_to_process, center, 3, center_color, -1, cv::LINE_AA);
    //             }
    //         }
    //         // 如果 circles 为空，则此 if 语句块不执行，屏幕上就只会留下刚刚画的黄色方框和实时视频。
    //     }
    //     catch (const cv::Exception &ex)
    //     {
    //         // ESP_LOGE(TAG, "OpenCV Exception: %s", ex.what());
    //         return;
    //     }
    // }
    else if (xEventGroupGetBits(camera_event_group) & CAMERA_EVENT_TEXT_DETECT)
    {
        // 1. 缓存同步，保证数据最新
        esp_cache_msync((void *)camera_buf, camera_buf_len, ESP_CACHE_MSYNC_FLAG_INVALIDATE);

        // 2. 定义ROI
        int full_width = camera_buf_hes;
        int full_height = camera_buf_ves;
        int roi_width = 128;
        int roi_height = 128;
        if (roi_width > full_width)
            roi_width = full_width;
        if (roi_height > full_height)
            roi_height = full_height;
        int roi_x = (full_width - roi_width) / 2;
        int roi_y = (full_height - roi_height) / 2;

        // 3. 创建ROI视图
        cv::Mat full_input_header(full_height, full_width, CV_8UC2, camera_buf);
        cv::Mat roi_to_process = full_input_header(cv::Rect(roi_x, roi_y, roi_width, roi_height));

        if (roi_to_process.empty())
        {
            return;
        }

        // 4. 必要的static Mat
        static cv::Mat roi_snapshot;
        static cv::Mat gray_roi;
        static std::vector<cv::Vec3f> circles;

        try
        {
            // 5. 捕获快照并进行检测
            roi_to_process.copyTo(roi_snapshot);
            cv::cvtColor(roi_snapshot, gray_roi, cv::COLOR_BGR5652GRAY);
            cv::GaussianBlur(gray_roi, gray_roi, cv::Size(5, 5), 2, 2);
            cv::HoughCircles(
                gray_roi, circles, cv::HOUGH_GRADIENT, 1,
                gray_roi.rows / 8, 80, 20, 5, 60);

            // --- 6. 最终的、根据您思路构建的 IF/ELSE 绘图逻辑 ---
            if (!circles.empty())
            {
                // 情况A：检测到圆
                // 我们在干净的快照上画上所有内容（方框+圆圈）
                cv::rectangle(roi_snapshot, cv::Point(0, 0), cv::Point(roi_width - 1, roi_height - 1), cv::Scalar(0xFFE0), 1); // 黄色边框

                cv::Scalar circle_color(0x07E0); // 绿色
                cv::Scalar center_color(0xF800); // 红色
                for (size_t i = 0; i < circles.size(); i++)
                {
                    cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
                    int radius = cvRound(circles[i][2]);
                    cv::circle(roi_snapshot, center, radius, circle_color, 2, cv::LINE_AA);
                    cv::circle(roi_snapshot, center, 3, center_color, -1, cv::LINE_AA);
                }
                // 然后将这张“完成品”一次性更新到屏幕
                roi_snapshot.copyTo(roi_to_process);
            }
            else
            {
                // 情况B：没有检测到圆 - 这是您要的“清除”操作
                // 我们在干净的快照上只画方框
                cv::rectangle(roi_snapshot, cv::Point(0, 0), cv::Point(roi_width - 1, roi_height - 1), cv::Scalar(0xFFE0), 1); // 黄色边框

                // 然后将这张“干净的背景+方框”的图像更新到屏幕
                // 这一步会用干净的背景覆盖掉上一帧可能残留的任何圆圈
                roi_snapshot.copyTo(roi_to_process);
            }
        }
        catch (const cv::Exception &ex)
        {
            // ESP_LOGE(TAG, "OpenCV Exception: %s", ex.what());
            return;
        }
    }

    if (lcddev.id <= 0x7084) /* RGB屏显示(4.3/7寸800480屏幕PCLK调整为16MHz，7寸1024600屏幕PCLK调整为16MHz) */
    {
        ppa_srm_oper_config_t deal_oper_config;
        deal_oper_config.in.buffer = (void *)camera_buf;
        deal_oper_config.in.pic_w = camera_buf_hes;
        deal_oper_config.in.pic_h = camera_buf_ves;
        deal_oper_config.in.block_w = camera_buf_hes / 2;                                        // 1280  640
        deal_oper_config.in.block_h = camera_buf_ves / 2;                                        // 960   480
        deal_oper_config.in.block_offset_x = (camera_buf_hes - deal_oper_config.in.block_w) / 2; // 选择从摄像头拍摄的中间区域
        deal_oper_config.in.block_offset_y = (camera_buf_ves - deal_oper_config.in.block_h) / 2; // 选择从摄像头拍摄的中间区域
        deal_oper_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

        deal_oper_config.out.buffer = draw_buffer;
        deal_oper_config.out.buffer_size = ALIGN_UP_BY(lcddev.width * lcddev.height * BSP_LCD_BITS_PER_PIXEL / 8, lcd_data_cache_line_size);
        deal_oper_config.out.pic_w = lcddev.width;
        deal_oper_config.out.pic_h = lcddev.height;
        deal_oper_config.out.block_offset_x = (lcddev.width - deal_oper_config.in.block_w) / 2;  // 80;
        deal_oper_config.out.block_offset_y = (lcddev.height - deal_oper_config.in.block_h) / 2; // 0;
        deal_oper_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        deal_oper_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        deal_oper_config.scale_x = 1;
        deal_oper_config.scale_y = 1;
        deal_oper_config.rgb_swap = 0;
        deal_oper_config.byte_swap = 0;
        deal_oper_config.mirror_x = true;
        deal_oper_config.mirror_y = false;
        deal_oper_config.mode = PPA_TRANS_MODE_BLOCKING;
        deal_oper_config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
        int ret = ppa_do_scale_rotate_mirror(ppa_deal_client_srm_handle, &deal_oper_config);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "ppa_do_scale_rotate_mirror 1 failed with error 0x%x", ret);
        }
    }
    else /* 5.5寸MIPI屏显示 */
    {
        ppa_srm_oper_config_t deal_oper_config;
        deal_oper_config.in.buffer = (void *)camera_buf;
        deal_oper_config.in.pic_w = camera_buf_hes;
        deal_oper_config.in.pic_h = camera_buf_ves;
        deal_oper_config.in.block_w = camera_buf_hes / 2;                                        // 1280  640
        deal_oper_config.in.block_h = camera_buf_ves / 1.5;                                      // 960   640
        deal_oper_config.in.block_offset_x = (camera_buf_hes - deal_oper_config.in.block_w) / 2; // 选择从摄像头拍摄的中间区域
        deal_oper_config.in.block_offset_y = (camera_buf_ves - deal_oper_config.in.block_h) / 2; // 选择从摄像头拍摄的中间区域
        deal_oper_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

        deal_oper_config.out.buffer = draw_buffer;
        deal_oper_config.out.buffer_size = ALIGN_UP_BY(lcddev.width * lcddev.height * BSP_LCD_BITS_PER_PIXEL / 8, lcd_data_cache_line_size);
        deal_oper_config.out.pic_w = lcddev.width;
        deal_oper_config.out.pic_h = lcddev.height;
        deal_oper_config.out.block_offset_x = (lcddev.width - deal_oper_config.in.block_w) / 2;  // 40;
        deal_oper_config.out.block_offset_y = (lcddev.height - deal_oper_config.in.block_h) / 2; // 320;
        deal_oper_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        deal_oper_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        deal_oper_config.scale_x = 1;
        deal_oper_config.scale_y = 1;
        deal_oper_config.rgb_swap = 0;
        deal_oper_config.byte_swap = 0;
        deal_oper_config.mirror_x = true;
        deal_oper_config.mirror_y = false;
        deal_oper_config.mode = PPA_TRANS_MODE_BLOCKING;
        deal_oper_config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
        int ret = ppa_do_scale_rotate_mirror(ppa_deal_client_srm_handle, &deal_oper_config);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "ppa_do_scale_rotate_mirror 1 failed with error 0x%x", ret);
        }
    }
}
