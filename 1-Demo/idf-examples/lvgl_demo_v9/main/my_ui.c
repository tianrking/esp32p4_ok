// my_ui.c
#include "my_ui.h"
#include "esp_log.h"
#include "lvgl.h"
#include "mqtt_client.h" 

static const char *TAG = "MY_UI"; // 与你原始 my_ui.c 中的 TAG 一致

// MQTT 主题和有效负载定义
#define MQTT_LIGHT1_COMMAND_TOPIC "s3w0x7ceswitch/switch/relay1/command"
#define MQTT_LIGHT2_COMMAND_TOPIC "s3w0x7ceswitch/switch/relay2/command"
#define MQTT_PAYLOAD_ON           "ON"
#define MQTT_PAYLOAD_OFF          "OFF"

// 按钮尺寸定义
#define DEVICE_BUTTON_WIDTH  230
#define DEVICE_BUTTON_HEIGHT 200

// 颜色定义
#define SCREEN_BACKGROUND_COLOR    lv_color_hex(0x101212)
#define BUTTON_OFF_COLOR           lv_color_hex(0x2C2C2E)
#define BUTTON_ON_COLOR            lv_color_hex(0x007AFF)
#define BUTTON_ICON_COLOR          lv_color_white()
#define BUTTON_TEXT_COLOR          lv_color_white()
#define BUTTON_CATEGORY_TEXT_COLOR lv_color_hex(0xAEAEB2)
#define BUTTON_RADIUS              16
#define BUTTON_SHADOW_OPA          LV_OPA_30
#define BUTTON_SHADOW_WIDTH        8
#define BUTTON_SHADOW_OFS_Y        3

typedef struct {
    bool is_on;                
    const char *name_text;      
    lv_obj_t *icon_label;      
    int mqtt_control_id;       
} device_state_t;

static lv_obj_t *btn_mqtt_light1_ref = NULL;
static lv_obj_t *btn_mqtt_light2_ref = NULL;

extern esp_mqtt_client_handle_t client;

static void update_button_visual_style(lv_obj_t *btn, bool is_on); 
static void device_button_event_cb(lv_event_t *e); 
static lv_obj_t *create_device_button(lv_obj_t *parent, 
                                       const char *icon_symbol, 
                                       const char *name_text_en,      
                                       const char *category_text_en,  
                                       bool initial_state, 
                                       int mqtt_id); 

static void _lvgl_update_mqtt_button_visual_async_cb(void *user_data) {
    uint32_t data = (uint32_t)(uintptr_t)user_data;
    bool is_on = data & 0x1;
    int light_id_internal = (data >> 1); 

    ESP_LOGD(TAG, "Async UI update for MQTT button (ID %d) to %s", light_id_internal, is_on ? "ON" : "OFF");
    lv_obj_t *target_btn = NULL;
    if (light_id_internal == 1) target_btn = btn_mqtt_light1_ref;
    else if (light_id_internal == 2) target_btn = btn_mqtt_light2_ref;

    if (target_btn) {
        device_state_t *state = (device_state_t *)lv_obj_get_user_data(target_btn);
        if (state) {
            state->is_on = is_on; 
        }
        update_button_visual_style(target_btn, is_on); 
    }
}

void my_ui_update_light_status_from_mqtt(int light_id, bool is_on) { 
    ESP_LOGI(TAG, "Received MQTT state update for Light (ID %d): %s", light_id, is_on ? "ON" : "OFF");
    uint32_t data = ((uint32_t)light_id << 1) | (is_on ? 1 : 0);
    lv_async_call(_lvgl_update_mqtt_button_visual_async_cb, (void*)(uintptr_t)data);
}

static void update_button_visual_style(lv_obj_t *btn, bool is_on) {
    if (!btn) return;
    lv_obj_set_style_bg_color(btn, is_on ? BUTTON_ON_COLOR : BUTTON_OFF_COLOR, LV_PART_MAIN);
}

static void device_button_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    device_state_t *state = (device_state_t *)lv_obj_get_user_data(btn);

    if (!state) {
        ESP_LOGE(TAG, "Button state is NULL for button!");
        return;
    }

    if (state->mqtt_control_id == 1 || state->mqtt_control_id == 2) {
        bool current_button_ui_state = state->is_on; 
        bool command_should_be_on = !current_button_ui_state; 

        const char* command_topic = (state->mqtt_control_id == 1) ? MQTT_LIGHT1_COMMAND_TOPIC : MQTT_LIGHT2_COMMAND_TOPIC;
        const char* payload_to_send = command_should_be_on ? MQTT_PAYLOAD_ON : MQTT_PAYLOAD_OFF;
        
        ESP_LOGI(TAG, "MQTT Button (ID %d, Name: '%s') pressed. Current UI state: %s. Sending command: %s to %s", 
                 state->mqtt_control_id, state->name_text, current_button_ui_state ? "ON" : "OFF", payload_to_send, command_topic);
                 
        if (client) {
            int msg_id = esp_mqtt_client_publish(client, command_topic, payload_to_send, 0, 1, false); 
            if (msg_id != -1) {
                ESP_LOGI(TAG, "MQTT command published for ID %d: %s, msg_id=%d", state->mqtt_control_id, payload_to_send, msg_id);
            } else {
                ESP_LOGE(TAG, "MQTT command publish FAILED for ID %d", state->mqtt_control_id);
            }
        } else {
            ESP_LOGE(TAG, "MQTT client not available for publishing command for ID %d", state->mqtt_control_id);
        }
    } else {
        state->is_on = !state->is_on; 
        if (state->is_on) {
            lv_obj_set_style_bg_color(btn, BUTTON_ON_COLOR, LV_PART_MAIN);
            ESP_LOGI(TAG, "Local Button '%s' (name from state) turned ON", state->name_text);
        } else {
            lv_obj_set_style_bg_color(btn, BUTTON_OFF_COLOR, LV_PART_MAIN);
            ESP_LOGI(TAG, "Local Button '%s' (name from state) turned OFF", state->name_text);
        }
    }
}

static lv_obj_t *create_device_button(lv_obj_t *parent, 
                                       const char *icon_symbol, 
                                       const char *name_text_en,      
                                       const char *category_text_en,  
                                       bool initial_state, 
                                       int mqtt_id) { 
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, DEVICE_BUTTON_WIDTH, DEVICE_BUTTON_HEIGHT);
    lv_obj_set_style_radius(btn, BUTTON_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN); 
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);     
    
    lv_obj_set_style_shadow_width(btn, BUTTON_SHADOW_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn, BUTTON_SHADOW_OPA, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(btn, BUTTON_SHADOW_OFS_Y, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN); 

    lv_obj_set_style_pad_all(btn, 15, LV_PART_MAIN);        

    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_26, LV_PART_MAIN); 
    lv_obj_set_style_text_color(icon, BUTTON_ICON_COLOR, LV_PART_MAIN);

    lv_obj_t *text_container = lv_obj_create(btn);
    lv_obj_remove_style_all(text_container); 
    lv_obj_set_width(text_container, LV_PCT(100)); 
    lv_obj_set_flex_flow(text_container, LV_FLEX_FLOW_COLUMN); 
    lv_obj_set_flex_align(text_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(text_container, 5, LV_PART_MAIN); 

    lv_obj_t *name_label = lv_label_create(text_container);
    lv_label_set_text(name_label, name_text_en); 
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, LV_PART_MAIN); 
    lv_obj_set_style_text_color(name_label, BUTTON_TEXT_COLOR, LV_PART_MAIN);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP); 
    lv_obj_set_width(name_label, LV_PCT(100));

    lv_obj_t *category_label = lv_label_create(text_container);
    lv_label_set_text(category_label, category_text_en); 
    lv_obj_set_style_text_font(category_label, &lv_font_montserrat_12, LV_PART_MAIN); 
    lv_obj_set_style_text_color(category_label, BUTTON_CATEGORY_TEXT_COLOR, LV_PART_MAIN);

    device_state_t *state_data = (device_state_t *)lv_malloc(sizeof(device_state_t));
    if (!state_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for device state for '%s'!", name_text_en);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_USER_1); 
        return btn; 
    }
    state_data->is_on = initial_state; 
    state_data->name_text = name_text_en;    
    state_data->icon_label = icon; 
    state_data->mqtt_control_id = mqtt_id; 

    lv_obj_set_user_data(btn, state_data); 

    if (initial_state) {
        lv_obj_set_style_bg_color(btn, BUTTON_ON_COLOR, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(btn, BUTTON_OFF_COLOR, LV_PART_MAIN);
    }
    
    lv_obj_add_event_cb(btn, device_button_event_cb, LV_EVENT_CLICKED, NULL); 

    return btn;
}

void my_ui_init(void) {
    ESP_LOGI(TAG, "Initializing My UI (Original Style, MQTT for first 2 buttons, English Text)");

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, SCREEN_BACKGROUND_COLOR, LV_PART_MAIN);

    lv_obj_t *main_container = lv_obj_create(scr);
    lv_obj_remove_style_all(main_container); 
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100)); 
    lv_obj_center(main_container); 

    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_all(main_container, 20, LV_PART_MAIN);        
    lv_obj_set_style_pad_row(main_container, 20, LV_PART_MAIN);        
    lv_obj_set_style_pad_column(main_container, 20, LV_PART_MAIN);     

    // --- 创建按钮 ---
    // 第一个按钮: MQTT 控制 (mqtt_id = 1)
    btn_mqtt_light1_ref = create_device_button(main_container, LV_SYMBOL_POWER, "Light Switch 1", "Remote Control", false, 1);

    // 第二个按钮: MQTT 控制 (mqtt_id = 2)
    btn_mqtt_light2_ref = create_device_button(main_container, LV_SYMBOL_POWER, "Light Switch 2", "Remote Control", false, 2);

    // 其他按钮 (本地控制, mqtt_id = 0), 保持你原始的英文文本
    create_device_button(main_container, LV_SYMBOL_SETTINGS, "Air Conditioner", "Environment", false, 0); 
    create_device_button(main_container, LV_SYMBOL_VIDEO, "Television", "Entertainment", false, 0);
    create_device_button(main_container, LV_SYMBOL_LIST, "Living Room Curtain", "Shading", true, 0); // 初始状态为true示例
    create_device_button(main_container, LV_SYMBOL_PLUS, "Humidifier", "Environment", false, 0); 
    create_device_button(main_container, LV_SYMBOL_BELL, "Robot Vacuum", "Cleaning", true, 0);
    create_device_button(main_container, LV_SYMBOL_OK, "Smart Lock", "Security", false, 0); 
    create_device_button(main_container, LV_SYMBOL_AUDIO, "Background Music", "Entertainment", true, 0);
    create_device_button(main_container, LV_SYMBOL_EDIT, "Garage Door", "Security", false, 0); 
    create_device_button(main_container, LV_SYMBOL_CHARGE, "Charging Post", "Energy", false, 0);
    create_device_button(main_container, LV_SYMBOL_WIFI, "Router", "Network", true, 0);
    create_device_button(main_container, LV_SYMBOL_SETTINGS, "Kitchen Light", "Lighting", true, 0); 
    create_device_button(main_container, LV_SYMBOL_POWER, "Main Power Switch", "System", false, 0);
    create_device_button(main_container, LV_SYMBOL_WARNING, "Smoke Detector", "Security", true, 0);

    ESP_LOGI(TAG, "My UI (Original Style, Hybrid Control, English Text) Initialized Successfully.");
    ESP_LOGI(TAG, "UI is waiting for MQTT state updates for Light Switch 1 & 2.");
}