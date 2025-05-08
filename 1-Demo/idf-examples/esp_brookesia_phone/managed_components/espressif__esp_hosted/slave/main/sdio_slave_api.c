// SPDX-License-Identifier: Apache-2.0
// Copyright 2015-2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_hosted_log.h"
#include "interface.h"
#include "adapter.h"
#include "sdio_slave_api.h"
#include "driver/sdio_slave.h"
#include "soc/sdio_slave_periph.h"
#include "endian.h"
#include "mempool.h"
#include "stats.h"

//#define SIMPLIFIED_SDIO_SLAVE          1
#define SDIO_SLAVE_QUEUE_SIZE            20
#define BUFFER_SIZE     	         MAX_TRANSPORT_BUF_SIZE
#define BUFFER_NUM      	         20
static uint8_t sdio_slave_rx_buffer[BUFFER_NUM][BUFFER_SIZE];

#define SDIO_MEMPOOL_NUM_BLOCKS          40
static struct hosted_mempool * buf_mp_tx_g;

interface_context_t context;
interface_handle_t if_handle_g;
static const char TAG[] = "SDIO_SLAVE";

#define SDIO_RX_QUEUE_SIZE           CONFIG_ESP_SDIO_RX_Q_SIZE

#if !SIMPLIFIED_SDIO_SLAVE
static SemaphoreHandle_t sdio_rx_sem;
static QueueHandle_t sdio_rx_queue[MAX_PRIORITY_QUEUES];
static SemaphoreHandle_t sdio_send_queue_sem = NULL; // to count number of Tx bufs in IDF SDIO driver
#endif

#define SDIO_SLAVE_TO_HOST_INT_BIT7     7
#define SDIO_SLAVE_TO_HOST_INT_BIT6     6
#define HOST_INT_START_THROTTLE      SDIO_SLAVE_TO_HOST_INT_BIT7
#define HOST_INT_STOP_THROTTLE       SDIO_SLAVE_TO_HOST_INT_BIT6

/* Note: Sometimes the SDIO card is detected but gets problem in
 * Read/Write or handling ISR because of SDIO timing issues.
 * In these cases, Please tune timing below via Menuconfig
 * */
#if CONFIG_ESP_SDIO_PSEND_PSAMPLE
#define SDIO_SLAVE_TIMING SDIO_SLAVE_TIMING_PSEND_PSAMPLE
#elif CONFIG_ESP_SDIO_NSEND_PSAMPLE
#define SDIO_SLAVE_TIMING SDIO_SLAVE_TIMING_NSEND_PSAMPLE
#elif CONFIG_ESP_SDIO_PSEND_NSAMPLE
#define SDIO_SLAVE_TIMING SDIO_SLAVE_TIMING_PSEND_NSAMPLE
#elif CONFIG_ESP_SDIO_NSEND_NSAMPLE
#define SDIO_SLAVE_TIMING SDIO_SLAVE_TIMING_NSEND_NSAMPLE
#else
#error No SDIO Slave Timing configured
#endif

static interface_handle_t * sdio_init(void);
static int32_t sdio_write(interface_handle_t *handle, interface_buffer_handle_t *buf_handle);
static int sdio_read(interface_handle_t *if_handle, interface_buffer_handle_t *buf_handle);
static esp_err_t sdio_reset(interface_handle_t *handle);
static void sdio_deinit(interface_handle_t *handle);
#if !SIMPLIFIED_SDIO_SLAVE
static void sdio_rx_task(void* pvParameters);
static void sdio_tx_done_task(void* pvParameters);
#endif

if_ops_t if_ops = {
	.init = sdio_init,
	.write = sdio_write,
	.read = sdio_read,
	.reset = sdio_reset,
	.deinit = sdio_deinit,
};

static inline void sdio_mempool_create(void)
{
	buf_mp_tx_g = hosted_mempool_create(NULL, 0, SDIO_MEMPOOL_NUM_BLOCKS, BUFFER_SIZE);
#ifdef CONFIG_ESP_CACHE_MALLOC
	assert(buf_mp_tx_g);
#endif
}

static inline void sdio_mempool_destroy(void)
{
	hosted_mempool_destroy(buf_mp_tx_g);
}

static inline void *sdio_buffer_tx_alloc(size_t nbytes, uint need_memset)
{
	/* TODO: When Mempool is not needed, SDIO should use
	 * exact bytes for allocation instead of BUFFER_SIZE
	 * To reduce strain on system memory */
	return hosted_mempool_alloc(buf_mp_tx_g, nbytes, need_memset);
}

static inline void sdio_buffer_tx_free(void *buf)
{
	hosted_mempool_free(buf_mp_tx_g, buf);
}

#if !SIMPLIFIED_SDIO_SLAVE
static void start_rx_data_throttling_if_needed(void)
{
	uint32_t queue_load;
	uint8_t load_percent;

	if (slv_cfg_g.throttle_high_threshold > 0) {

		/* Already throttling, nothing to be done */
		if (slv_state_g.current_throttling)
			return;


		queue_load = uxQueueMessagesWaiting(sdio_rx_queue[PRIO_Q_OTHERS]);
#if ESP_PKT_STATS
		pkt_stats.slave_wifi_rx_msg_loaded = queue_load;
#endif

		load_percent = (queue_load*100/SDIO_RX_QUEUE_SIZE);
		if (load_percent > slv_cfg_g.throttle_high_threshold) {
			slv_state_g.current_throttling = 1;
			ESP_LOGV(TAG, "start data throttling at host");
			sdio_slave_send_host_int(HOST_INT_START_THROTTLE);
		}
	}
}

static void stop_rx_data_throttling_if_needed(void)
{
	uint32_t queue_load;
	uint8_t load_percent;

	if (slv_state_g.current_throttling) {

		queue_load = uxQueueMessagesWaiting(sdio_rx_queue[PRIO_Q_OTHERS]);
#if ESP_PKT_STATS
		pkt_stats.slave_wifi_rx_msg_loaded = queue_load;
#endif

		load_percent = (queue_load*100/SDIO_RX_QUEUE_SIZE);
		if (load_percent < slv_cfg_g.throttle_low_threshold) {
			slv_state_g.current_throttling = 0;
			ESP_LOGV(TAG, "stop data throttling at host");
			sdio_slave_send_host_int(HOST_INT_STOP_THROTTLE);
		}
	}
}
#endif

interface_context_t *interface_insert_driver(int (*event_handler)(uint8_t val))
{
	ESP_LOGI(TAG, "Using SDIO interface");
	memset(&context, 0, sizeof(context));

	context.type = SDIO;
	context.if_ops = &if_ops;
	context.event_handler = event_handler;

	return &context;
}

int interface_remove_driver()
{
	memset(&context, 0, sizeof(context));
	return 0;
}

IRAM_ATTR static void event_cb(uint8_t val)
{
	if (val == ESP_RESET) {
		sdio_reset(&if_handle_g);
		return;
	}

	if (context.event_handler) {
		context.event_handler(val);
	}
}

void generate_startup_event(uint8_t cap, uint32_t ext_cap)
{
	struct esp_payload_header *header = NULL;
	interface_buffer_handle_t buf_handle = {0};
	struct esp_priv_event *event = NULL;
	uint8_t *pos = NULL;
	uint16_t len = 0;
	uint8_t raw_tp_cap = 0;
	esp_err_t ret = ESP_OK;

	raw_tp_cap = debug_get_raw_tp_conf();

	memset(&buf_handle, 0, sizeof(buf_handle));

	buf_handle.payload = sdio_buffer_tx_alloc(512, MEMSET_REQUIRED);
	assert(buf_handle.payload);

	header = (struct esp_payload_header *) buf_handle.payload;

	header->if_type = ESP_PRIV_IF;
	header->if_num = 0;
	header->offset = htole16(sizeof(struct esp_payload_header));
	header->priv_pkt_type = ESP_PACKET_TYPE_EVENT;

	/* Populate event data */
	event = (struct esp_priv_event *) (buf_handle.payload + sizeof(struct esp_payload_header));

	event->event_type = ESP_PRIV_EVENT_INIT;

	/* Populate TLVs for event */
	pos = event->event_data;

	/* TLVs start */

	/* TLV - Board type */
	*pos = ESP_PRIV_FIRMWARE_CHIP_ID;   pos++;len++;
	*pos = LENGTH_1_BYTE;               pos++;len++;
	*pos = CONFIG_IDF_FIRMWARE_CHIP_ID; pos++;len++;

	/* TLV - Capability */
	*pos = ESP_PRIV_CAPABILITY;         pos++;len++;
	*pos = LENGTH_1_BYTE;               pos++;len++;
	*pos = cap;                         pos++;len++;

	*pos = ESP_PRIV_TEST_RAW_TP;        pos++;len++;
	*pos = LENGTH_1_BYTE;               pos++;len++;
	*pos = raw_tp_cap;                  pos++;len++;

	*pos = ESP_PRIV_RX_Q_SIZE;          pos++;len++;
	*pos = LENGTH_1_BYTE;               pos++;len++;
	*pos = SDIO_RX_QUEUE_SIZE;          pos++;len++;
	/* TLVs end */

	event->event_len = len;

	/* payload len = Event len + sizeof(event type) + sizeof(event len) */
	len += 2;
	header->len = htole16(len);

	buf_handle.payload_len = len + sizeof(struct esp_payload_header);
#if CONFIG_ESP_SDIO_CHECKSUM
	header->checksum = htole16(compute_checksum(buf_handle.payload, buf_handle.payload_len));
#endif

	ESP_HEXLOGD("sdio_tx_init", buf_handle.payload, buf_handle.payload_len);

#if !SIMPLIFIED_SDIO_SLAVE
	xSemaphoreTake(sdio_send_queue_sem, portMAX_DELAY);
	ret = sdio_slave_send_queue(buf_handle.payload, buf_handle.payload_len,
			buf_handle.payload, portMAX_DELAY);
#else
	ret = sdio_slave_transmit(buf_handle.payload, buf_handle.payload_len);
#endif
	if (ret != ESP_OK) {
		ESP_LOGE(TAG , "sdio slave tx error, ret : 0x%x\r\n", ret);
		sdio_buffer_tx_free(buf_handle.payload);
		return;
	}
#if SIMPLIFIED_SDIO_SLAVE
	sdio_buffer_tx_free(buf_handle.payload);
#endif

}

static void sdio_read_done(void *handle)
{
	sdio_slave_recv_load_buf((sdio_slave_buf_handle_t) handle);
}

static interface_handle_t * sdio_init(void)
{
#if !SIMPLIFIED_SDIO_SLAVE
	uint16_t prio_q_idx = 0;
#endif
	esp_err_t ret = ESP_OK;
	sdio_slave_buf_handle_t handle = {0};
	sdio_slave_config_t config = {
#if CONFIG_ESP_SDIO_STREAMING_MODE
		.sending_mode       = SDIO_SLAVE_SEND_STREAM,
#else
		.sending_mode       = SDIO_SLAVE_SEND_PACKET,
#endif
		.send_queue_size    = SDIO_SLAVE_QUEUE_SIZE,
		.recv_buffer_size   = BUFFER_SIZE,
		.event_cb           = event_cb,

		/* Note: For small devkits there may be no pullups on the board.
		   This enables the internal pullups to help evaluate the driver
		   quickly. However the internal pullups are not sufficient and not
		   reliable, please make sure external pullups are connected to the
		   bus in your real design.
		   */
		//.flags              = SDIO_SLAVE_FLAG_INTERNAL_PULLUP,
#if CONFIG_ESP_SDIO_DEFAULT_SPEED
		.flags              = SDIO_SLAVE_FLAG_DEFAULT_SPEED,
#elif CONFIG_ESP_SDIO_HIGH_SPEED
		.flags              = SDIO_SLAVE_FLAG_HIGH_SPEED,
#else
#error Invalid SDIO bus speed selection
#endif
  		.timing             = SDIO_SLAVE_TIMING,
	};

#if CONFIG_ESP_SDIO_STREAMING_MODE
	ESP_LOGI(TAG, "%s: sending mode: SDIO_SLAVE_SEND_STREAM", __func__);
#else
	ESP_LOGI(TAG, "%s: sending mode: SDIO_SLAVE_SEND_PACKET", __func__);
#endif
#if defined(CONFIG_IDF_TARGET_ESP32C6)
	ESP_LOGI(TAG, "%s: ESP32-C6 SDIO RxQ[%d] timing[%u]\n", __func__, SDIO_RX_QUEUE_SIZE, config.timing);
#else
	ESP_LOGI(TAG, "%s: ESP32 SDIO RxQ[%d] timing[%u]\n", __func__, SDIO_RX_QUEUE_SIZE, config.timing);
#endif

#if !SIMPLIFIED_SDIO_SLAVE
	sdio_send_queue_sem = xSemaphoreCreateCounting(SDIO_SLAVE_QUEUE_SIZE, SDIO_SLAVE_QUEUE_SIZE);
	assert(sdio_send_queue_sem);

	sdio_rx_sem = xSemaphoreCreateCounting(SDIO_RX_QUEUE_SIZE*3, 0);
	assert(sdio_rx_sem != NULL);

	for (prio_q_idx=0; prio_q_idx<MAX_PRIORITY_QUEUES;prio_q_idx++) {
		sdio_rx_queue[prio_q_idx] = xQueueCreate(SDIO_RX_QUEUE_SIZE, sizeof(interface_buffer_handle_t));
		assert(sdio_rx_queue[prio_q_idx] != NULL);
	}
#endif
	ret = sdio_slave_initialize(&config);
	if (ret != ESP_OK) {
		return NULL;
	}


	for (int i = 0; i < BUFFER_NUM; i++) {
		handle = sdio_slave_recv_register_buf(sdio_slave_rx_buffer[i]);
		assert(handle != NULL);

		ret = sdio_slave_recv_load_buf(handle);
		if (ret != ESP_OK) {
			sdio_slave_deinit();
			return NULL;
		}
	}

	/* ESP-Hosted uses bit6 and bit 7 internal use. Rest free for Users */
	sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET |
			SDIO_SLAVE_HOSTINT_BIT0 |
			SDIO_SLAVE_HOSTINT_BIT1 |
			SDIO_SLAVE_HOSTINT_BIT2 |
			SDIO_SLAVE_HOSTINT_BIT3 |
			SDIO_SLAVE_HOSTINT_BIT4 |
			SDIO_SLAVE_HOSTINT_BIT5 |
			SDIO_SLAVE_HOSTINT_BIT6 |
			SDIO_SLAVE_HOSTINT_BIT7);

	ret = sdio_slave_start();
	if (ret != ESP_OK) {
		sdio_slave_deinit();
		return NULL;
	}

	memset(&if_handle_g, 0, sizeof(if_handle_g));

	sdio_mempool_create();
	if_handle_g.state = INIT;

#if !SIMPLIFIED_SDIO_SLAVE
	assert(xTaskCreate(sdio_rx_task, "sdio_rx_task" ,
			CONFIG_ESP_DEFAULT_TASK_STACK_SIZE, NULL,
			CONFIG_ESP_DEFAULT_TASK_PRIO, NULL) == pdTRUE);

	// task to clean up after doing sdio tx
	assert(xTaskCreate(sdio_tx_done_task, "sdio_tx_done_task" ,
			CONFIG_ESP_DEFAULT_TASK_STACK_SIZE, NULL,
			CONFIG_ESP_DEFAULT_TASK_PRIO, NULL) == pdTRUE);
#endif
	return &if_handle_g;
}

/* wait for sdio to finish tx, then free the buffer */
#if !SIMPLIFIED_SDIO_SLAVE
static void sdio_tx_done_task(void* pvParameters)
{
	esp_err_t res;
	uint8_t sendbuf = 0;
	uint8_t *sendbuf_p = &sendbuf;

	while (true) {
		res = sdio_slave_send_get_finished((void**)&sendbuf_p, portMAX_DELAY);
		if (res) {
			ESP_LOGE(TAG, "sdio_slave_send_get_finished() error");
			continue;
		}
		xSemaphoreGive(sdio_send_queue_sem);
		sdio_buffer_tx_free(sendbuf_p);
	}
}
#endif

static int32_t sdio_write(interface_handle_t *handle, interface_buffer_handle_t *buf_handle)
{
	esp_err_t ret = ESP_OK;
	int32_t total_len = 0;
	uint8_t* sendbuf = NULL;
	uint16_t offset = sizeof(struct esp_payload_header);
	struct esp_payload_header *header = NULL;

	if (!handle || !buf_handle) {
		ESP_LOGE(TAG , "Invalid arguments");
		return ESP_FAIL;
	}

	if (handle->state != ACTIVE) {
		return ESP_FAIL;
	}

	if (!buf_handle->payload_len || !buf_handle->payload) {
		ESP_LOGE(TAG , "Invalid arguments, len:%d", buf_handle->payload_len);
		return ESP_FAIL;
	}

	total_len = buf_handle->payload_len + offset;

	sendbuf = sdio_buffer_tx_alloc(total_len, MEMSET_REQUIRED);
	if (sendbuf == NULL) {
		ESP_LOGE(TAG , "send buffer[%"PRIu32"] malloc fail", total_len);
		return ESP_FAIL;
	}

	header = (struct esp_payload_header *) sendbuf;

	memset (header, 0, sizeof(struct esp_payload_header));

	/* Initialize header */
	header->if_type = buf_handle->if_type;
	header->if_num = buf_handle->if_num;
	header->len = htole16(buf_handle->payload_len);
	header->offset = htole16(offset);
	header->seq_num = htole16(buf_handle->seq_num);
	header->flags = buf_handle->flag;

	memcpy(sendbuf + offset, buf_handle->payload, buf_handle->payload_len);

#if CONFIG_ESP_SDIO_CHECKSUM
	header->checksum = htole16(compute_checksum(sendbuf,
				offset+buf_handle->payload_len));
#endif

	ESP_HEXLOGD("sdio_tx", sendbuf, min(32,total_len));

#if !SIMPLIFIED_SDIO_SLAVE
	xSemaphoreTake(sdio_send_queue_sem, portMAX_DELAY);
	ret = sdio_slave_send_queue(sendbuf, total_len, sendbuf, portMAX_DELAY);
#else
	ret = sdio_slave_transmit(sendbuf, total_len);
#endif
	if (ret != ESP_OK) {
		ESP_LOGE(TAG , "sdio slave transmit error, ret : 0x%x\r\n", ret);
		sdio_buffer_tx_free(sendbuf);
		return ESP_FAIL;
	}

#if SIMPLIFIED_SDIO_SLAVE
	sdio_buffer_tx_free(sendbuf);
#endif

#if ESP_PKT_STATS
	if (header->if_type == ESP_STA_IF)
		pkt_stats.sta_sh_out++;
	else if (header->if_type == ESP_SERIAL_IF)
		pkt_stats.serial_tx_total++;
#endif
	return buf_handle->payload_len;
}

#if !SIMPLIFIED_SDIO_SLAVE
static int sdio_read(interface_handle_t *if_handle, interface_buffer_handle_t *buf_handle)
{
	if (!if_handle || (if_handle->state != ACTIVE) || !buf_handle) {
		ESP_LOGE(TAG, "%s: Invalid state/args", __func__);
		return ESP_FAIL;
	}

	xSemaphoreTake(sdio_rx_sem, portMAX_DELAY);

	if (pdFALSE == xQueueReceive(sdio_rx_queue[PRIO_Q_SERIAL], buf_handle, 0))
		if (pdFALSE == xQueueReceive(sdio_rx_queue[PRIO_Q_BT], buf_handle, 0))
			if (pdFALSE == xQueueReceive(sdio_rx_queue[PRIO_Q_OTHERS], buf_handle, 0)) {
				ESP_LOGE(TAG, "%s No element in rx queue", __func__);
		return ESP_FAIL;
	}

	stop_rx_data_throttling_if_needed();

	return buf_handle->payload_len;
}

static void sdio_rx_task(void* pvParameters)
{
	esp_err_t ret = ESP_OK;
	struct esp_payload_header *header = NULL;
#if CONFIG_ESP_SDIO_CHECKSUM
	uint16_t rx_checksum = 0, checksum = 0;
#endif
	uint16_t len = 0, offset = 0;
	size_t sdio_read_len = 0;
	interface_buffer_handle_t buf_handle = {0};

	for(;;) {

		ret = sdio_slave_recv(&(buf_handle.sdio_buf_handle), &(buf_handle.payload),
				&(sdio_read_len), portMAX_DELAY);
		if (ret) {
			ESP_LOGE(TAG, "sdio_slave_recv returned failure");
			continue;
		}

		buf_handle.payload_len = sdio_read_len & 0xFFFF;

		header = (struct esp_payload_header *) buf_handle.payload;

		len = le16toh(header->len);
		if (!len) {
			ESP_LOGE(TAG, "sdio_slave_recv returned 0 len");
			sdio_read_done(buf_handle.sdio_buf_handle);
			continue;
		}

		offset = le16toh(header->offset);

		if (buf_handle.payload_len < len+offset) {
			ESP_LOGE(TAG, "%s: err: read_len[%u] < len[%u]+offset[%u]", __func__,
					buf_handle.payload_len, len, offset);
			sdio_read_done(buf_handle.sdio_buf_handle);
			continue;
		}

#if CONFIG_ESP_SDIO_CHECKSUM
		rx_checksum = le16toh(header->checksum);
		header->checksum = 0;

		checksum = compute_checksum(buf_handle.payload, len+offset);

		if (checksum != rx_checksum) {
			ESP_LOGE(TAG, "sdio rx calc_chksum[%u] != exp_chksum[%u], drop pkt", checksum, rx_checksum);
			sdio_read_done(buf_handle.sdio_buf_handle);
			continue;
		}
#endif

		buf_handle.if_type = header->if_type;
		buf_handle.if_num = header->if_num;
		buf_handle.free_buf_handle = sdio_read_done;

		start_rx_data_throttling_if_needed();

#if ESP_PKT_STATS
	if (header->if_type == ESP_STA_IF)
		pkt_stats.hs_bus_sta_in++;
#endif
		if (header->if_type == ESP_SERIAL_IF) {
			xQueueSend(sdio_rx_queue[PRIO_Q_SERIAL], &buf_handle, portMAX_DELAY);
		} else if (header->if_type == ESP_HCI_IF) {
			xQueueSend(sdio_rx_queue[PRIO_Q_BT], &buf_handle, portMAX_DELAY);
		} else {
			xQueueSend(sdio_rx_queue[PRIO_Q_OTHERS], &buf_handle, portMAX_DELAY);
		}

		xSemaphoreGive(sdio_rx_sem);
	}
}
#else /* !SIMPLIFIED_SDIO_SLAVE */
static int sdio_read(interface_handle_t *if_handle, interface_buffer_handle_t *buf_handle)
{
	esp_err_t ret = ESP_OK;
	struct esp_payload_header *header = NULL;
#if CONFIG_ESP_SDIO_CHECKSUM
	uint16_t rx_checksum = 0, checksum = 0;
#endif
	uint16_t len = 0;
	size_t sdio_read_len = 0;


	if (!if_handle || !buf_handle) {
		ESP_LOGE(TAG, "Invalid arguments to sdio_read");
		return ESP_FAIL;
	}

	if (if_handle->state != ACTIVE)
		return ESP_FAIL;

	ret = sdio_slave_recv(&(buf_handle->sdio_buf_handle), &(buf_handle->payload),
			&(sdio_read_len), portMAX_DELAY);
	if (ret) {
		ESP_LOGD(TAG, "sdio_slave_recv returned failure");
		return ESP_FAIL;
	}

	buf_handle->payload_len = sdio_read_len & 0xFFFF;

	header = (struct esp_payload_header *) buf_handle->payload;

	len = le16toh(header->len) + le16toh(header->offset);

#if CONFIG_ESP_SDIO_CHECKSUM
	rx_checksum = le16toh(header->checksum);
	header->checksum = 0;

	checksum = compute_checksum(buf_handle->payload, len);

	if (checksum != rx_checksum) {
		ESP_LOGE(TAG, "sdio rx calc_chksum[%u] != exp_chksum[%u], drop pkt", checksum, rx_checksum);
		sdio_read_done(buf_handle->sdio_buf_handle);
		return ESP_FAIL;
	}
#endif
#if ESP_PKT_STATS
	if (header->if_type == ESP_STA_IF)
		pkt_stats.hs_bus_sta_in++;
#endif

	buf_handle->if_type = header->if_type;
	buf_handle->if_num = header->if_num;
	buf_handle->free_buf_handle = sdio_read_done;
	return len;
}
#endif /* !SIMPLIFIED_SDIO_SLAVE */

static esp_err_t sdio_reset(interface_handle_t *handle)
{
	esp_err_t ret = ESP_OK;

	sdio_slave_stop();

	ret = sdio_slave_reset();
	if (ret != ESP_OK)
		return ret;

	/* ESP-Hosted uses bit6 and bit 7 internal use, rest bits free */
	sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET |
			SDIO_SLAVE_HOSTINT_BIT0 |
			SDIO_SLAVE_HOSTINT_BIT1 |
			SDIO_SLAVE_HOSTINT_BIT2 |
			SDIO_SLAVE_HOSTINT_BIT3 |
			SDIO_SLAVE_HOSTINT_BIT4 |
			SDIO_SLAVE_HOSTINT_BIT5 |
			SDIO_SLAVE_HOSTINT_BIT6 |
			SDIO_SLAVE_HOSTINT_BIT7);

	ret = sdio_slave_start();
	if (ret != ESP_OK)
		return ret;

	while (1) {
		sdio_slave_buf_handle_t handle = NULL;

		/* Return buffers to driver */
		ret = sdio_slave_send_get_finished(&handle, 0);
		if (ret != ESP_OK)
			break;
#if !SIMPLIFIED_SDIO_SLAVE
		xSemaphoreGive(sdio_send_queue_sem);
#endif

		if (handle) {
			ret = sdio_slave_recv_load_buf(handle);
			ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
		}
	}

	return ESP_OK;
}

static void sdio_deinit(interface_handle_t *handle)
{
	sdio_mempool_destroy();
	sdio_slave_stop();
	sdio_slave_reset();
}
