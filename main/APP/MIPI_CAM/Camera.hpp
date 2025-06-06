/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_video.h"
#include "esp_video_init.h"

#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/imgcodecs/legacy/constants_c.h>
#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/imgproc.hpp>

#include <opencv2/core/utility.hpp>

#include "opencv2/opencv_modules.hpp"

#include "opencv2/core.hpp"
#include <opencv2/core/cuda.hpp>
#include <opencv2/core/cvstd.hpp>
#include <opencv2/core/softfloat.hpp>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/imgproc/imgproc_c.h>

#include <iostream>
#include <math.h>


class Camera {
public:
    Camera(uint16_t hor_res, uint16_t ver_res);
    ~Camera();
    bool run(void);
    bool init(void);
private:
    static void taskCameraInit(Camera *app);
    static void camera_dectect_task(Camera *app);
    uint16_t _hor_res;
    uint16_t _ver_res;
    SemaphoreHandle_t _camera_init_sem;
    int _camera_ctlr_handle;
    TaskHandle_t _detect_task_handle;
    uint8_t *_cam_buffer[EXAMPLE_CAM_BUF_NUM];
    size_t _cam_buffer_size[EXAMPLE_CAM_BUF_NUM];

    // void *draw_buffer;
    void *lcddev_buffer[EXAMPLE_CAM_BUF_NUM];
};

