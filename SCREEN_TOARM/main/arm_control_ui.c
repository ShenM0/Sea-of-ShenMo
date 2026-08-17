/*
 * Servo Speed Control - Simple 3-Button UI
 * MSPM0 protocol: S+050 (CCW), S-050 (CW), S000 (stop)
 */

#include "arm_control.h"
#include "lvgl.h"

static const char *TAG = "servo_ui";

static void fwd_cb(lv_event_t *e)  { arm_send_speed(50); }
static void stop_cb(lv_event_t *e) { arm_send_speed(0); }
static void rev_cb(lv_event_t *e)  { arm_send_speed(-50); }

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text,
                           lv_color_t color, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, lv_pct(80), 80);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);
    return btn;
}

void arm_control_ui_create(void)
{
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A2E), 0);

    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 20, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(root, 16, 0);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "SERVO CONTROL");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    make_btn(root, "FWD",  lv_color_hex(0x4CAF50), fwd_cb);
    make_btn(root, "STOP", lv_color_hex(0xE63946), stop_cb);
    make_btn(root, "REV",  lv_color_hex(0x2196F3), rev_cb);

    ESP_LOGI(TAG, "Servo UI created");
}
