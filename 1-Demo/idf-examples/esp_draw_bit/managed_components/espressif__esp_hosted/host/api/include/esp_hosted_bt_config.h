/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ESP_HOSTED_BT_CONFIG_H__
#define __ESP_HOSTED_BT_CONFIG_H__

// Hosted BT defines for NimBLE
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
#define H_BT_HOST_ESP_NIMBLE 1
#else
#define H_BT_HOST_ESP_NIMBLE 0
#endif

#if CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI
#define H_BT_USE_VHCI 1
#else
#define H_BT_USE_VHCI 0
#endif

// Hosted BT defines for BlueDroid
#if CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#define H_BT_HOST_ESP_BLUEDROID 1
#else
#define H_BT_HOST_ESP_BLUEDROID 0
#endif

#if CONFIG_ESP_HOSTED_BLUEDROID_HCI_VHCI
#define H_BT_BLUEDROID_USE_VHCI 1
#else
#define H_BT_BLUEDROID_USE_VHCI 1
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
// ll_init required
#define H_BT_ENABLE_LL_INIT 1
#else
#define H_BT_ENABLE_LL_INIT 0
#endif

// check: only one BT host stack can be enabled at a time
#if H_BT_HOST_ESP_NIMBLE && H_BT_HOST_ESP_BLUEDROID
#error "Enable only NimBLE or BlueDroid, not both"
#endif

#endif
