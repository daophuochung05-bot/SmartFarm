#pragma once
//  ui — TOÀN BỘ giao diện: màu/font, 3 màn hình (chính / chi tiết / cài đặt), báo động, modal.
//  Không có gì dính LVGL nằm ngoài file này, trừ driver màn hình (CYD_library.h).
//  Mục lục các phần nằm ngay dưới đây, tìm theo số.
//
//  LUẬT: mọi hàm ở đây chạy trên LUỒNG LVGL. Task mạng (core 0) TUYỆT ĐỐI không gọi vào.
//  Lệnh web đi đường vòng: Mqtt xếp hàng đợi -> luồng LVGL lấy ra -> ui_apply_*.
//
//   1. MÀU / FONT / KÍCH THƯỚC
//   2. STATE DÙNG CHUNG
//   3. BÁO ĐỘNG
//   4. HEADER (tên thiết bị + 2 đèn báo)
//   5. CHẤM TRANG
//   6. BƠM TUẦN HOÀN
//   7. Ô THÔNG SỐ
//   8. MODAL THÊM/SỬA CÂY (tên + logo)
//   9. TRANG CÂY
//  10. TRANG CUỐI: THÊM CÂY + CÀI ĐẶT
//  11. DỰNG MÀN + NHỊP VẼ LẠI
//  12. MÀN CHI TIẾT — state riêng
//  13. NGƯỠNG (2 đường kẻ trên biểu đồ + 2 ô số)
//  14. BÀN PHÍM SỐ
//  15. CHẾ ĐỘ TAY
//  16. WIDGET DÙNG LẠI
//  17. BIỂU ĐỒ
//  18. MỞ / ĐÓNG MÀN
//  19. MODAL ĐỔI TÊN THIẾT BỊ
//  20. MÀN NHẬP LIỆU (dùng chung cho SSID / mật khẩu / Chat ID)
//  21. MÀN CÀI ĐẶT
//  22. KHỞI TẠO
//  23. CỬA NHẬN LỆNH TỪ WEB

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <lvgl.h>
#include "AppData.h"
#include "AppSensor.h"
#include "AppWiFi.h"

#include "AppTheme.h"

//  ══════ 2. STATE DÙNG CHUNG ══════

typedef struct {
    lv_obj_t* val_label[P_COUNT];
    lv_obj_t* box[P_COUNT];
    lv_obj_t* dot[P_COUNT];
    lv_obj_t* manbadge[P_COUNT];
} CropTileWidgets;

static std::vector<CropTileWidgets> tile_widgets;
static std::vector<lv_obj_t*>       pump_switches;   // mỗi tile 1 switch, phải đồng bộ với nhau

static lv_obj_t* scr_main        = NULL;
static lv_obj_t* tileview        = NULL;
static lv_obj_t* dots_cont       = NULL;
static lv_obj_t* lbl_device_name = NULL;
static lv_obj_t* lbl_wifi_led    = NULL;
static lv_obj_t* lbl_sensor_led  = NULL;

static lv_obj_t*   alarm_overlay = NULL;
static bool        alarm_blink_on = false;

// ---------------- Hàm nội bộ giữa 3 màn ----------------
static void ui_build_main_screen();
static void ui_update_dots();
static void ui_open_device_name_modal();

//  ══════ 3. BÁO ĐỘNG ══════

static void ui_build_alarm_overlay() {
    if (alarm_overlay) return;
    alarm_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(alarm_overlay);
    lv_obj_set_size(alarm_overlay, 320, 240);
    lv_obj_set_pos(alarm_overlay, 0, 0);
    lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(alarm_overlay, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(alarm_overlay, LV_OPA_TRANSP, 0);
}

static void ui_alarm_update_overlay() {
    if (!alarm_overlay) return;
    bool any = false;
    for (int i = 0; i < P_COUNT; i++) if (param_alarm[i]) { any = true; break; }
    //  Có bất kỳ thông số nào vượt ngưỡng thì phủ lớp đỏ mờ, không thì trong suốt
    if (any) lv_obj_set_style_bg_opa(alarm_overlay, LV_OPA_20, 0);
    else     lv_obj_set_style_bg_opa(alarm_overlay, LV_OPA_TRANSP, 0);
}

// Nháy VIỀN CẢ Ô, không nháy chấm 6x6 — liếc từ xa mới thấy được.
static void ui_alarm_blink_cb(lv_timer_t* t) {
    alarm_blink_on = !alarm_blink_on;
    if (active_crop < 0 || active_crop >= (int)tile_widgets.size()) return;

    CropTileWidgets* pw = &tile_widgets[active_crop];
    for (int i = 0; i < P_COUNT; i++) {
        if (!param_alarm[i] || !pw->box[i]) continue;
        //  Nhấp nháy viền ô: lúc sáng thì viền đỏ dày, lúc tối thì viền thường
        if (alarm_blink_on) {
            lv_obj_set_style_border_color(pw->box[i], COL_BAD, 0);
            lv_obj_set_style_border_width(pw->box[i], 3, 0);
        } else {
            lv_obj_set_style_border_color(pw->box[i], COL_BORDER, 0);
            lv_obj_set_style_border_width(pw->box[i], 1, 0);
        }
    }
}

static void ui_alarm_clear(int crop_idx, int param_idx) {
    param_alarm[param_idx] = false;
    if (crop_idx >= 0 && crop_idx < (int)tile_widgets.size()) {
        lv_obj_t* b = tile_widgets[crop_idx].box[param_idx];
        if (b) {
            lv_obj_set_style_border_color(b, COL_BORDER, 0);
            lv_obj_set_style_border_width(b, 1, 0);
        }
    }
    ui_alarm_update_overlay();
}

//  ══════ 4. HEADER (tên thiết bị + 2 đèn báo) ══════
static void ui_header_name_click_cb(lv_event_t* e) { ui_open_device_name_modal(); }

static lv_obj_t* ui_create_header(lv_obj_t* parent) {
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 320, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, ui_header_name_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(header, 8);

    lbl_device_name = lv_label_create(header);
    lv_label_set_text(lbl_device_name, device_name);
    lv_obj_set_style_text_font(lbl_device_name, FONT_TITLE, 0);
    lv_obj_set_style_text_color(lbl_device_name, COL_TEXT, 0);
    lv_obj_center(lbl_device_name);
    lv_obj_add_flag(lbl_device_name, LV_OBJ_FLAG_EVENT_BUBBLE);

    lbl_wifi_led = lv_label_create(header);
    lv_label_set_text(lbl_wifi_led, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi_led, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_wifi_led, COL_DOT, 0);
    lv_obj_align(lbl_wifi_led, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_flag(lbl_wifi_led, LV_OBJ_FLAG_EVENT_BUBBLE);

    lbl_sensor_led = lv_label_create(header);
    lv_label_set_text(lbl_sensor_led, "SEN");
    lv_obj_set_style_text_font(lbl_sensor_led, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_sensor_led, COL_OFF, 0);
    lv_obj_align(lbl_sensor_led, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(lbl_sensor_led, LV_OBJ_FLAG_EVENT_BUBBLE);

    return header;
}

// WiFi: xám = chưa có mạng | cam = có WiFi chưa lên MQTT | xanh = web thấy được
// SEN : đỏ = chưa ghép | cam = mất sóng | xanh = đang nhận số | SIM xanh dương = mô phỏng
static void ui_header_status_cb(lv_timer_t* t) {
    if (lbl_wifi_led) {
        //  Đèn WiFi: xám = chưa có mạng, cam = có WiFi nhưng chưa lên MQTT, xanh = web thấy được
        lv_color_t c;
        if      (!wifi_up())   c = COL_DOT;
        else if (!mqtt_online) c = COL_WARN;
        else                   c = COL_ON;
        lv_obj_set_style_text_color(lbl_wifi_led, c, 0);
    }

    if (!lbl_sensor_led) return;

    if (sensor_source == SOURCE_SIM) {
        lv_label_set_text(lbl_sensor_led, "SIM");
        lv_obj_set_style_text_color(lbl_sensor_led, COL_ACCENT, 0);
    } else {
        lv_label_set_text(lbl_sensor_led, "SEN");
        bool paired = sensorRadio.hasPeer();
        bool fresh  = paired && sensor_last_rx_ms &&
                      (millis() - sensor_last_rx_ms < SENSOR_TIMEOUT_MS);
        //  Đèn cảm biến: đỏ = chưa ghép, cam = ghép rồi nhưng mất sóng, xanh = đang nhận số
        lv_color_t c;
        if      (!paired) c = COL_OFF;
        else if (!fresh)  c = COL_WARN;
        else              c = COL_ON;
        lv_obj_set_style_text_color(lbl_sensor_led, c, 0);
    }
}

//  ══════ 5. CHẤM TRANG ══════
static void ui_update_dots() {
    if (!dots_cont) return;
    lv_obj_clean(dots_cont);
    int total = (int)crops.size() + 1;          // +1 = trang "thêm cây"
    for (int i = 0; i < total; i++) {
        lv_obj_t* dot = lv_obj_create(dots_cont);
        lv_obj_remove_style_all(dot);
        //  Chấm của trang đang xem thì dài + sáng, các chấm khác thì ngắn + mờ
        bool active = (i == current_page);
        if (active) lv_obj_set_size(dot, 16, 4);
        else        lv_obj_set_size(dot, 4, 4);
        lv_obj_set_style_radius(dot, 2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        if (active) lv_obj_set_style_bg_color(dot, COL_TEXT, 0);
        else        lv_obj_set_style_bg_color(dot, COL_DOT, 0);
    }
}

static void ui_goto_page(int idx, bool anim) {
    if (!tileview) return;
    int total = (int)crops.size() + 1;
    if (idx < 0)      idx = 0;
    if (idx >= total) idx = total - 1;

    //  Chuyển trang có hoạt ảnh trượt hay không
    if (anim) lv_obj_set_tile_id(tileview, idx, 0, LV_ANIM_ON);
    else      lv_obj_set_tile_id(tileview, idx, 0, LV_ANIM_OFF);
    current_page = idx;
    ui_update_dots();
}

//  ══════ 6. BƠM TUẦN HOÀN ══════
// Bơm là 1 thiết bị vật lý nhưng mỗi tile vẽ 1 switch riêng -> 1 cái đổi, mấy cái kia nhảy theo.
static void ui_pump_sync_switches() {
    for (lv_obj_t* sw : pump_switches) {
        if (!sw) continue;
        if (lv_obj_has_state(sw, LV_STATE_CHECKED) == pump_on) continue;
        if (pump_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else         lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
}

static void ui_pump_sw_cb(lv_event_t* e) {
    pump_on        = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    pump_lock_ms   = millis();
    cloud_push_now = true;
    ui_pump_sync_switches();
    storage_save_config();
}

//  ══════ 7. Ô THÔNG SỐ ══════
static void ui_gauge_click_cb(lv_event_t* e) {
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    ui_open_detail((int)(code % 100000), (int)(code / 100000));   // (crop, param)
}

static void ui_create_widget_card(lv_obj_t* parent, int crop_idx, int param_idx, CropTileWidgets* pw) {
    ParamMeta* meta = &param_meta[param_idx];

    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 92, 62);
    lv_obj_set_style_bg_color(box, COL_CANVAS, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, COL_BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, ui_gauge_click_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(param_idx * 100000 + crop_idx));
    pw->box[param_idx] = box;

    lv_obj_t* dot = lv_obj_create(box);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_bg_color(dot, meta->color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 8, 8);
    pw->dot[param_idx] = dot;

    // Huy hiệu "M" = thông số này đang ở chế độ tay.
    lv_obj_t* manbadge = lv_label_create(box);
    lv_label_set_text(manbadge, "M");
    lv_obj_set_style_text_font(manbadge, FONT_SMALL, 0);
    lv_obj_set_style_text_color(manbadge, lv_color_hex(0x1D1400), 0);
    lv_obj_set_style_bg_color(manbadge, COL_WARN, 0);
    lv_obj_set_style_bg_opa(manbadge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(manbadge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(manbadge, 3, 0);
    lv_obj_set_style_pad_ver(manbadge, 1, 0);
    lv_obj_align(manbadge, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_add_flag(manbadge, LV_OBJ_FLAG_HIDDEN);
    pw->manbadge[param_idx] = manbadge;

    lv_obj_t* lbl_name = lv_label_create(box);
    lv_label_set_text(lbl_name, meta->short_name);
    lv_obj_set_style_text_font(lbl_name, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_name, COL_TEXT_DIM, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 18, 5);

    lv_obj_t* lbl_val = lv_label_create(box);
    lv_label_set_text(lbl_val, "--");
    lv_obj_set_style_text_font(lbl_val, FONT_VALUE, 0);
    lv_obj_set_style_text_color(lbl_val, meta->color, 0);
    lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    pw->val_label[param_idx] = lbl_val;

    if (meta->unit[0]) {
        lv_obj_t* lbl_unit = lv_label_create(box);
        lv_label_set_text(lbl_unit, meta->unit);
        lv_obj_set_style_text_font(lbl_unit, FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_unit, COL_TEXT_DIM, 0);
        lv_obj_align(lbl_unit, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    }
}

//  ══════ 8. MODAL THÊM/SỬA CÂY (tên + logo) ══════
static lv_obj_t* crop_modal_bg  = NULL;
static lv_obj_t* crop_modal_ta  = NULL;
static lv_obj_t* crop_logo_btn  = NULL;
static lv_obj_t* crop_modal_kb  = NULL;
static lv_obj_t* crop_logo_pick = NULL;
static int       editing_crop   = -1;
static int       temp_logo_idx  = 0;

static void ui_close_crop_modal() {
    if (!crop_modal_bg) return;
    lv_obj_del(crop_modal_bg);
    crop_modal_bg = NULL; crop_modal_ta = NULL; crop_logo_btn = NULL;
    crop_modal_kb = NULL; crop_logo_pick = NULL;
}

static void ui_confirm_crop_modal(lv_event_t* e) {
    const char* text = lv_textarea_get_text(crop_modal_ta);
    if (!text || !text[0]) text = "New Crop";

    if (editing_crop >= 0) {
        strncpy(crops[editing_crop].name, text, sizeof(crops[0].name) - 1);
        crops[editing_crop].logo     = CROP_LOGOS[temp_logo_idx];
        crops[editing_crop].logo_idx = temp_logo_idx;
        ui_close_crop_modal();
        ui_rebuild_tiles();
        ui_goto_page(editing_crop, false);
    } else {
        Crop p;
        memset(&p, 0, sizeof(p));
        strncpy(p.name, text, sizeof(p.name) - 1);
        p.logo     = CROP_LOGOS[temp_logo_idx];
        p.logo_idx = temp_logo_idx;
        for (int i = 0; i < P_COUNT; i++) {
            p.setpoint[i] = LETTUCE_SETPOINT[i];
            p.value[i]    = p.setpoint[i];
            p.lo_limit[i] = clamp_phys(i, param_meta[i].lo_limit);
            p.hi_limit[i] = clamp_phys(i, param_meta[i].hi_limit);
        }
        crops.push_back(p);
        ui_close_crop_modal();
        ui_rebuild_tiles();
        ui_goto_page((int)crops.size() - 1, true);
    }
    storage_save_config();
}

static void ui_crop_kb_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if      (code == LV_EVENT_READY)  ui_confirm_crop_modal(e);
    else if (code == LV_EVENT_CANCEL) ui_close_crop_modal();
}

static void ui_draw_logo_on(lv_obj_t* parent, int idx) {
    if (CROP_LOGOS[idx]) {
        lv_obj_t* img = lv_img_create(parent);
        lv_img_set_src(img, CROP_LOGOS[idx]);
        lv_obj_center(img);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
    } else {
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, LV_SYMBOL_DIRECTORY);
        lv_obj_center(lbl);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void ui_logo_picked_cb(lv_event_t* e) {
    temp_logo_idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_clean(crop_logo_btn);
    ui_draw_logo_on(crop_logo_btn, temp_logo_idx);
}

// Bàn phím và bảng chọn logo dùng CHUNG nửa dưới màn hình -> hiện cái này thì giấu cái kia.
static void ui_toggle_logo_picker_cb(lv_event_t* e) {
    if (crop_modal_kb)  lv_obj_add_flag(crop_modal_kb, LV_OBJ_FLAG_HIDDEN);
    if (crop_logo_pick) lv_obj_clear_flag(crop_logo_pick, LV_OBJ_FLAG_HIDDEN);
}

static void ui_crop_ta_focus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_FOCUSED && code != LV_EVENT_CLICKED) return;
    if (crop_logo_pick) lv_obj_add_flag(crop_logo_pick, LV_OBJ_FLAG_HIDDEN);
    if (crop_modal_kb)  lv_obj_clear_flag(crop_modal_kb, LV_OBJ_FLAG_HIDDEN);
}

static void ui_open_crop_modal(int crop_idx) {
    editing_crop  = crop_idx;
    temp_logo_idx = 1;
    const char* default_text = "";

    if (crop_idx >= 0) {
        default_text = crops[crop_idx].name;
        for (int i = 0; i < NUM_CROP_LOGOS; i++)
            if (CROP_LOGOS[i] == crops[crop_idx].logo) { temp_logo_idx = i; break; }
    }

    crop_modal_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(crop_modal_bg, 320, 240);
    lv_obj_set_style_bg_color(crop_modal_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(crop_modal_bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(crop_modal_bg, 0, 0);
    lv_obj_clear_flag(crop_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(crop_modal_bg, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_t* card = lv_obj_create(crop_modal_bg);
    lv_obj_set_size(card, 280, 75);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, COL_CANVAS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_t* lbl = lv_label_create(card);
    //  crop_idx >= 0 nghĩa là đang sửa cây có sẵn, ngược lại là thêm cây mới
    if (crop_idx >= 0) lv_label_set_text(lbl, "Edit Crop:");
    else               lv_label_set_text(lbl, "Add New Crop:");
    lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT_DIM, 0);
    lv_obj_set_pos(lbl, 30, 8);

    crop_logo_btn = lv_btn_create(card);
    lv_obj_set_size(crop_logo_btn, 32, 32);
    lv_obj_set_pos(crop_logo_btn, 30, 28);
    lv_obj_set_style_pad_all(crop_logo_btn, 0, 0);
    lv_obj_set_style_bg_color(crop_logo_btn, COL_BORDER, 0);
    lv_obj_set_style_radius(crop_logo_btn, 6, 0);
    lv_obj_add_event_cb(crop_logo_btn, ui_toggle_logo_picker_cb, LV_EVENT_CLICKED, NULL);
    ui_draw_logo_on(crop_logo_btn, temp_logo_idx);

    crop_modal_ta = lv_textarea_create(card);
    lv_textarea_set_one_line(crop_modal_ta, true);
    lv_obj_clear_flag(crop_modal_ta, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(crop_modal_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(crop_modal_ta, 6, 0);
    lv_obj_set_size(crop_modal_ta, 178, 32);
    lv_obj_set_pos(crop_modal_ta, 72, 28);
    lv_textarea_set_text(crop_modal_ta, default_text);
    lv_obj_add_event_cb(crop_modal_ta, ui_crop_ta_focus_cb, LV_EVENT_ALL, NULL);

    crop_modal_kb = lv_keyboard_create(crop_modal_bg);
    lv_obj_set_size(crop_modal_kb, 320, 134);
    lv_obj_align(crop_modal_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(crop_modal_kb, crop_modal_ta);
    lv_obj_add_event_cb(crop_modal_kb, ui_crop_kb_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(crop_modal_kb, ui_crop_kb_cb, LV_EVENT_CANCEL, NULL);

    crop_logo_pick = lv_obj_create(crop_modal_bg);
    lv_obj_set_size(crop_logo_pick, 320, 134);
    lv_obj_align(crop_logo_pick, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(crop_logo_pick, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(crop_logo_pick, COL_CANVAS, 0);
    lv_obj_set_style_border_width(crop_logo_pick, 0, 0);
    lv_obj_add_flag(crop_logo_pick, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(crop_logo_pick, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(crop_logo_pick, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(crop_logo_pick, 10, 0);
    lv_obj_set_style_pad_gap(crop_logo_pick, 15, 0);

    for (int i = 0; i < NUM_CROP_LOGOS; i++) {
        lv_obj_t* lbtn = lv_btn_create(crop_logo_pick);
        lv_obj_set_size(lbtn, 50, 50);
        lv_obj_set_style_pad_all(lbtn, 0, 0);
        lv_obj_set_style_bg_color(lbtn, COL_CARD, 0);
        lv_obj_add_event_cb(lbtn, ui_logo_picked_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        ui_draw_logo_on(lbtn, i);
    }

    lv_obj_add_state(crop_modal_ta, LV_STATE_FOCUSED);
}

//  ══════ 9. TRANG CÂY ══════
static int crop_to_delete = -1;
static const char* del_mbox_btns[] = { "Yes", "No", "" };

static void ui_edit_name_click_cb(lv_event_t* e) {
    ui_open_crop_modal((int)(intptr_t)lv_event_get_user_data(e));
}

static void ui_delete_confirm_cb(lv_event_t* e) {
    lv_obj_t* mbox = (lv_obj_t*)lv_event_get_current_target(e);
    const char* txt = lv_msgbox_get_active_btn_text(mbox);

    if (txt && !strcmp(txt, "Yes") && crop_to_delete >= 0 &&
        (size_t)crop_to_delete < crops.size() && crops.size() > 1) {

        if      (active_crop == crop_to_delete) active_crop = 0;
        else if (active_crop >  crop_to_delete) active_crop--;

        crops.erase(crops.begin() + crop_to_delete);
        ui_rebuild_tiles();

        int idx = crop_to_delete;
        if (idx >= (int)crops.size()) idx = (int)crops.size() - 1;
        if (idx < 0) idx = 0;
        ui_goto_page(idx, false);
        storage_save_config();
    }
    crop_to_delete = -1;
    lv_msgbox_close(mbox);
}

static void ui_delete_tab_click_cb(lv_event_t* e) {
    int crop_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (crops.size() <= 1) return;                  // phải còn ít nhất 1 cây

    crop_to_delete = crop_idx;
    char msg[64];
    snprintf(msg, sizeof(msg), "Delete crop \"%s\"?", crops[crop_idx].name);
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Confirm", msg, del_mbox_btns, false);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, ui_delete_confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

// Chọn cây = cây này mới là cây được đọc cảm biến + gác báo động.
static void ui_select_btn_click_cb(lv_event_t* e) {
    int crop_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (active_crop == crop_idx) return;

    active_crop = crop_idx;
    for (int i = 0; i < P_COUNT; i++) param_alarm[i] = false;
    ui_rebuild_tiles();
    ui_alarm_update_overlay();
    ui_goto_page(crop_idx, false);
    storage_save_config();
}

static void ui_create_crop_tile(lv_obj_t* tile, int crop_idx) {
    Crop* pl = &crops[crop_idx];
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tile, 0, 0);

    // ---- hàng trên: tên cây + nút chọn + nút xoá ----
    lv_obj_t* top_row = lv_obj_create(tile);
    lv_obj_remove_style_all(top_row);
    lv_obj_set_size(top_row, 304, 25);
    lv_obj_align(top_row, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* name_btn = lv_btn_create(top_row);
    lv_obj_remove_style_all(name_btn);
    lv_obj_set_size(name_btn, 160, 24);
    lv_obj_align(name_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_flag(name_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(name_btn, ui_edit_name_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)crop_idx);

    if (pl->logo) {
        lv_obj_t* img = lv_img_create(name_btn);
        lv_img_set_src(img, pl->logo);
        lv_obj_align(img, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    lv_obj_t* name_lbl = lv_label_create(name_btn);
    lv_obj_set_style_text_font(name_lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(name_lbl, COL_TEXT, 0);
    lv_obj_add_flag(name_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    char buf[40];
    if (pl->logo) {
        snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_EDIT, pl->name);
        lv_label_set_text(name_lbl, buf);
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 36, 0);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_DIRECTORY " %s  " LV_SYMBOL_EDIT, pl->name);
        lv_label_set_text(name_lbl, buf);
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    if (crops.size() > 1) {
        lv_obj_t* del_btn = lv_obj_create(top_row);
        lv_obj_remove_style_all(del_btn);
        lv_obj_set_size(del_btn, 24, 24);
        lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(del_btn, ui_delete_tab_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)crop_idx);

        lv_obj_t* del_ic = lv_label_create(del_btn);
        lv_label_set_text(del_ic, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(del_ic, COL_BAD, 0);
        lv_obj_center(del_ic);
    }

    bool is_selected = (crop_idx == active_crop);
    lv_obj_t* sel_btn = lv_obj_create(top_row);
    lv_obj_remove_style_all(sel_btn);
    lv_obj_set_size(sel_btn, 24, 24);
    //  Nếu có nhiều hơn 1 cây thì chừa chỗ cho nút xóa nên nút chọn lùi trái 30px
    int sel_off = 0;
    if (crops.size() > 1) sel_off = -30;
    lv_obj_align(sel_btn, LV_ALIGN_RIGHT_MID, sel_off, 0);
    lv_obj_add_flag(sel_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(sel_btn, 12, 0);
    //  Cây đang chọn thì nút sáng đặc, chưa chọn thì trong suốt
    if (is_selected) lv_obj_set_style_bg_opa(sel_btn, LV_OPA_COVER, 0);
    else             lv_obj_set_style_bg_opa(sel_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(sel_btn, COL_ACCENT, 0);
    lv_obj_add_event_cb(sel_btn, ui_select_btn_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)crop_idx);

    lv_obj_t* sel_ic = lv_label_create(sel_btn);
    lv_label_set_text(sel_ic, LV_SYMBOL_OK);
    if (is_selected) lv_obj_set_style_text_color(sel_ic, COL_BG, 0);
    else             lv_obj_set_style_text_color(sel_ic, COL_TEXT_DIM, 0);
    lv_obj_center(sel_ic);

    // ---- thẻ chính: 6 ô thông số + bơm ----
    lv_obj_t* main_card = lv_obj_create(tile);
    lv_obj_remove_style_all(main_card);
    lv_obj_set_size(main_card, 304, 175);
    lv_obj_align(main_card, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(main_card, COL_CARD, 0);
    lv_obj_set_style_bg_opa(main_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(main_card, 16, 0);
    lv_obj_set_style_border_width(main_card, 1, 0);
    lv_obj_set_style_border_color(main_card, COL_BORDER, 0);
    lv_obj_clear_flag(main_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* grid = lv_obj_create(main_card);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 288, 130);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 6, 0);

    CropTileWidgets pw = {};
    for (int i = 0; i < P_COUNT; i++) ui_create_widget_card(grid, crop_idx, i, &pw);
    tile_widgets[crop_idx] = pw;

    lv_obj_t* pump_row = lv_obj_create(main_card);
    lv_obj_remove_style_all(pump_row);
    lv_obj_set_size(pump_row, 288, 30);
    lv_obj_align(pump_row, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(pump_row, COL_CANVAS, 0);
    lv_obj_set_style_bg_opa(pump_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pump_row, COL_BORDER, 0);
    lv_obj_set_style_border_width(pump_row, 1, 0);
    lv_obj_set_style_radius(pump_row, 10, 0);

    lv_obj_t* lbl_pump = lv_label_create(pump_row);
    lv_label_set_text(lbl_pump, "Circulation Pump");
    lv_obj_set_style_text_font(lbl_pump, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_pump, COL_ACCENT, 0);
    lv_obj_align(lbl_pump, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t* pump_sw = lv_switch_create(pump_row);
    lv_obj_set_size(pump_sw, 40, 22);
    lv_obj_align(pump_sw, LV_ALIGN_RIGHT_MID, -8, 0);
    if (pump_on) lv_obj_add_state(pump_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(pump_sw, ui_pump_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    pump_switches.push_back(pump_sw);
}

//  ══════ 10. TRANG CUỐI: THÊM CÂY + CÀI ĐẶT ══════
static void ui_add_tab_click_cb(lv_event_t* e)  { ui_open_crop_modal(-1); }
static void ui_settings_click_cb(lv_event_t* e) { ui_open_settings(); }

static void ui_create_add_tile(lv_obj_t* tile) {
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);

    lv_obj_t* gear_btn = lv_obj_create(tile);
    lv_obj_remove_style_all(gear_btn);
    lv_obj_set_size(gear_btn, 28, 28);
    lv_obj_align(gear_btn, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_radius(gear_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gear_btn, COL_CARD, 0);
    lv_obj_set_style_bg_opa(gear_btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(gear_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gear_btn, ui_settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* gear_ic = lv_label_create(gear_btn);
    lv_label_set_text(gear_ic, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gear_ic, COL_TEXT_DIM, 0);
    lv_obj_center(gear_ic);

    lv_obj_t* btn = lv_obj_create(tile);
    lv_obj_set_size(btn, 64, 64);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, COL_CANVAS, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, COL_BORDER, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, ui_add_tab_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* plus = lv_label_create(btn);
    lv_label_set_text(plus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(plus, COL_TEXT, 0);
    lv_obj_center(plus);

    lv_obj_t* hint = lv_label_create(tile);
    lv_label_set_text(hint, "Add Crop");
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, COL_TEXT_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 24);
}

//  ══════ 11. DỰNG MÀN + NHỊP VẼ LẠI ══════
static void ui_rebuild_tiles() {
    if (!tileview) return;
    lv_obj_clean(tileview);
    pump_switches.clear();
    tile_widgets.assign(crops.size(), CropTileWidgets{});

    for (size_t i = 0; i < crops.size(); i++)
        ui_create_crop_tile(lv_tileview_add_tile(tileview, (uint32_t)i, 0, LV_DIR_HOR), (int)i);

    ui_create_add_tile(lv_tileview_add_tile(tileview, (uint32_t)crops.size(), 0, LV_DIR_HOR));
    ui_update_dots();
}

static void ui_tileview_scroll_cb(lv_event_t* e) {
    lv_coord_t w = lv_obj_get_width(tileview);
    if (w <= 0) return;

    int idx = (int)((lv_obj_get_scroll_x(tileview) + w / 2) / w);
    if (idx == current_page) return;

    current_page = idx;
    ui_update_dots();
    storage_save_config();
}

static void ui_build_main_screen() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, COL_BG, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = ui_create_header(scr_main);

    dots_cont = lv_obj_create(scr_main);
    lv_obj_remove_style_all(dots_cont);
    lv_obj_set_size(dots_cont, 320, DOTS_H);
    lv_obj_align(dots_cont, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots_cont, 6, 0);

    tileview = lv_tileview_create(scr_main);
    lv_obj_set_size(tileview, 320, TILE_H);
    lv_obj_align(tileview, LV_ALIGN_TOP_MID, 0, HEADER_H);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tileview, 0, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview, ui_tileview_scroll_cb, LV_EVENT_SCROLL_END, NULL);

    lv_obj_move_foreground(header);
    ui_rebuild_tiles();
}

// 1s/lần: đổ số lên 6 ô, gác ngưỡng, bật/tắt báo động. KHÔNG sinh số — đó là việc của Sensor.
static void ui_refresh_cb(lv_timer_t* t) {
    for (size_t c = 0; c < crops.size(); c++) {

        if ((int)c == active_crop) {
            for (int i = 0; i < P_COUNT; i++) {
                if (sensor_valid[i]) crops[c].value[i] = sensor_value[i];

                float ev;
                bool  ok  = param_eff((int)c, i, &ev);
                bool  bad = ok && (ev < crops[c].lo_limit[i] || ev > crops[c].hi_limit[i]);

                if      (bad && !param_alarm[i]) { param_alarm[i] = true; ui_alarm_update_overlay(); }
                else if (!bad && param_alarm[i]) { ui_alarm_clear((int)c, i); }
            }
        }

        if (c >= tile_widgets.size()) continue;
        CropTileWidgets* pw = &tile_widgets[c];

        for (int i = 0; i < P_COUNT; i++) {
            if (pw->val_label[i]) {
                char buf[16];
                float ev;
                if (param_eff((int)c, i, &ev)) fmt_val(i, ev, buf, sizeof(buf));
                else                           strcpy(buf, "--");
                lv_label_set_text(pw->val_label[i], buf);
            }
            if (pw->manbadge[i]) {
                if (crops[c].manual[i]) lv_obj_clear_flag(pw->manbadge[i], LV_OBJ_FLAG_HIDDEN);
                else                    lv_obj_add_flag(pw->manbadge[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

//  ══════ 12. MÀN CHI TIẾT — state riêng ══════

static lv_obj_t* scr_detail  = NULL;
static int       detail_crop  = -1;
static int       detail_param = -1;

static lv_obj_t*         chart      = NULL;
static lv_chart_series_t* series    = NULL;
static lv_chart_series_t* series_lo = NULL;
static lv_chart_series_t* series_hi = NULL;
static lv_obj_t* chart_ph    = NULL;
static lv_obj_t* cur_val_lbl = NULL;
static lv_timer_t* chart_timer = NULL;

static lv_obj_t* lo_box = NULL, *hi_box = NULL;
static lv_obj_t* lo_lbl = NULL, *hi_lbl = NULL;

static lv_obj_t* sw_manual = NULL, *lbl_manual = NULL;
static lv_obj_t* sw_inc    = NULL, *sw_dec     = NULL;

static lv_obj_t* kp_bg = NULL, *kp_ta = NULL, *kp_kb = NULL;
static int       kp_target = 0;         // 0 = Lo, 1 = Hi

//  ══════ 13. NGƯỠNG (2 đường kẻ trên biểu đồ + 2 ô số) ══════
// Trục Y tự co giãn theo Lo/Hi + giá trị hiện tại, cộng lề 12% -> đường kẻ không dính mép.
static void ui_detail_update_lines() {
    if (!chart || !series_lo || !series_hi) return;

    ParamMeta* meta = &param_meta[detail_param];
    float lo = get_lo_limit(detail_crop, detail_param);
    float hi = get_hi_limit(detail_crop, detail_param);

    lv_chart_set_all_value(chart, series_lo, (int32_t)(lo * 10));
    lv_chart_set_all_value(chart, series_hi, (int32_t)(hi * 10));

    float v      = lo;
    bool  have_v = param_eff(detail_crop, detail_param, &v);
    if (!have_v && detail_crop >= 0 && detail_crop < (int)crops.size()) {
        v = crops[detail_crop].value[detail_param];
        have_v = true;
    }

    float y0 = lo, y1 = hi;
    if (have_v) {
        if (v < y0) y0 = v;
        if (v > y1) y1 = v;
    }

    float margin = (y1 - y0) * 0.12f;
    if (margin <= 0.0001f) margin = (meta->max_v - meta->min_v) * 0.05f;
    y0 = clampf(y0 - margin, meta->min_v, meta->max_v);
    y1 = clampf(y1 + margin, meta->min_v, meta->max_v);

    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)(y0 * 10), (int32_t)(y1 * 10));
    lv_chart_refresh(chart);
}

static void ui_detail_update_labels() {
    char num[16];
    fmt_val(detail_param, get_lo_limit(detail_crop, detail_param), num, sizeof(num));
    if (lo_lbl) lv_label_set_text(lo_lbl, num);

    fmt_val(detail_param, get_hi_limit(detail_crop, detail_param), num, sizeof(num));
    if (hi_lbl) lv_label_set_text(hi_lbl, num);
}

// Nhãn giá trị hiện tại (góc dưới biểu đồ). Chưa có số -> "--".
static void ui_detail_update_value() {
    if (!cur_val_lbl) return;

    ParamMeta* meta = &param_meta[detail_param];
    char num[16], buf[24];
    float ev;

    if (param_eff(detail_crop, detail_param, &ev)) {
        //  Có số: ghép số với đơn vị (nếu thông số này có đơn vị)
        fmt_val(detail_param, ev, num, sizeof(num));
        if (meta->unit[0]) snprintf(buf, sizeof(buf), "%s %s", num, meta->unit);
        else               snprintf(buf, sizeof(buf), "%s", num);
    } else {
        //  Chưa có số: hiện "--"
        if (meta->unit[0]) snprintf(buf, sizeof(buf), "-- %s", meta->unit);
        else               snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(cur_val_lbl, buf);
}

static void ui_detail_refresh_manual();

// Lệnh từ web đổi ngưỡng/chế độ tay -> nếu đang mở đúng màn này thì vẽ lại ngay.
static void ui_detail_refresh_limits() {
    if (!scr_detail || detail_crop != active_crop || detail_param < 0) return;
    ui_detail_update_lines();
    ui_detail_update_labels();
    ui_detail_refresh_manual();
    ui_detail_update_value();
}

static void ui_apply_limit_value(int target, float v) {
    if (detail_param < 0 || detail_param >= P_COUNT) return;

    v = clamp_phys(detail_param, v);
    float lo = get_lo_limit(detail_crop, detail_param);
    float hi = get_hi_limit(detail_crop, detail_param);

    if (target == 0) { if (v > hi) v = hi; set_lo_limit(detail_crop, detail_param, v); }
    else             { if (v < lo) v = lo; set_hi_limit(detail_crop, detail_param, v); }

    ui_detail_update_lines();
    ui_detail_update_labels();

    limit_lock_ms[detail_param] = millis();
    cloud_push_now = true;
    storage_save_config();
}

//  ══════ 14. BÀN PHÍM SỐ ══════
static void ui_close_keypad() {
    if (!kp_bg) return;
    lv_obj_del(kp_bg);
    kp_bg = NULL; kp_ta = NULL; kp_kb = NULL;
}

static void ui_keypad_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char* txt = lv_textarea_get_text(kp_ta);
        if (txt && txt[0]) ui_apply_limit_value(kp_target, (float)atof(txt));
        ui_close_keypad();
    } else if (code == LV_EVENT_CANCEL) {
        ui_close_keypad();
    }
}

static void ui_open_keypad(int target) {
    if (detail_param < 0 || detail_param >= P_COUNT) return;
    kp_target = target;
    ParamMeta* meta = &param_meta[detail_param];

    kp_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(kp_bg, 320, 240);
    lv_obj_set_style_bg_color(kp_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(kp_bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(kp_bg, 0, 0);
    lv_obj_set_style_pad_all(kp_bg, 0, 0);
    lv_obj_clear_flag(kp_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(kp_bg);
    lv_obj_set_size(card, 300, 64);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_border_color(card, COL_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cap = lv_label_create(card);
    char cbuf[40];
    //  target == 0 là đang sửa ngưỡng dưới (Lo), ngược lại là ngưỡng trên (Hi)
    const char* lohi;
    if (target == 0) lohi = "Lo";
    else             lohi = "Hi";
    const char* gap;
    if (meta->unit[0]) gap = "  ";
    else               gap = "";
    snprintf(cbuf, sizeof(cbuf), "%s  %s%s%s", lohi, meta->full_name, gap, meta->unit);
    lv_label_set_text(cap, cbuf);
    lv_obj_set_style_text_font(cap, FONT_SMALL, 0);
    //  Ngưỡng dưới tô cam, ngưỡng trên tô đỏ
    if (target == 0) lv_obj_set_style_text_color(cap, COL_WARN, 0);
    else             lv_obj_set_style_text_color(cap, COL_BAD, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 2, 0);

    kp_ta = lv_textarea_create(card);
    lv_textarea_set_one_line(kp_ta, true);
    lv_textarea_set_accepted_chars(kp_ta, "0123456789.");
    lv_obj_set_size(kp_ta, 280, 30);
    lv_obj_align(kp_ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(kp_ta, LV_OBJ_FLAG_SCROLLABLE);

    //  Điền sẵn giá trị ngưỡng hiện tại vào ô nhập
    char cur[16];
    float cur_val;
    if (target == 0) cur_val = get_lo_limit(detail_crop, detail_param);
    else             cur_val = get_hi_limit(detail_crop, detail_param);
    fmt_val(detail_param, cur_val, cur, sizeof(cur));
    lv_textarea_set_text(kp_ta, cur);

    kp_kb = lv_keyboard_create(kp_bg);
    lv_keyboard_set_mode(kp_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kp_kb, 320, 150);
    lv_obj_align(kp_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kp_kb, kp_ta);
    lv_obj_add_event_cb(kp_kb, ui_keypad_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kp_kb, ui_keypad_cb, LV_EVENT_CANCEL, NULL);
}

static void ui_limit_box_click_cb(lv_event_t* e) {
    ui_open_keypad((int)(intptr_t)lv_event_get_user_data(e));
}

//  ══════ 15. CHẾ ĐỘ TAY ══════
static void ui_detail_refresh_manual() {
    if (!sw_manual) return;
    if (detail_crop < 0 || detail_crop >= (int)crops.size()) return;

    Crop& pl = crops[detail_crop];
    bool  man = pl.manual[detail_param];

    if (man) lv_obj_add_state(sw_manual, LV_STATE_CHECKED);
    else     lv_obj_clear_state(sw_manual, LV_STATE_CHECKED);

    if (lbl_manual) {
        //  Đang bật chế độ tay thì hiện "Manual Mode" màu xanh, ngược lại "Auto Mode" màu mờ
        if (man) {
            lv_label_set_text(lbl_manual, "Manual Mode");
            lv_obj_set_style_text_color(lbl_manual, COL_ON, 0);
        } else {
            lv_label_set_text(lbl_manual, "Auto Mode");
            lv_obj_set_style_text_color(lbl_manual, COL_TEXT_DIM, 0);
        }
    }

    // Không ở chế độ tay -> 2 switch bơm bị KHOÁ (và xám hẳn, xem ui_make_switch).
    lv_obj_t* sws[2] = { sw_inc, sw_dec };
    bool      on[2]  = { pl.sw1[detail_param], pl.sw2[detail_param] };
    for (int k = 0; k < 2; k++) {
        if (!sws[k]) continue;
        if (man && on[k]) lv_obj_add_state(sws[k], LV_STATE_CHECKED);
        else              lv_obj_clear_state(sws[k], LV_STATE_CHECKED);
        if (man) lv_obj_clear_state(sws[k], LV_STATE_DISABLED);
        else     lv_obj_add_state(sws[k], LV_STATE_DISABLED);
    }
}

static void ui_manual_mode_cb(lv_event_t* e) {
    if (detail_crop < 0 || detail_crop >= (int)crops.size()) return;
    if (detail_param < 0 || detail_param >= P_COUNT) return;

    Crop& pl = crops[detail_crop];
    bool  man = lv_obj_has_state(sw_manual, LV_STATE_CHECKED);
    pl.manual[detail_param] = man;
    if (!man) {                                  // tắt tay -> tắt luôn 2 bơm
        pl.sw1[detail_param] = false;
        pl.sw2[detail_param] = false;
    }

    manual_lock_ms[detail_param] = millis();
    cloud_push_now = true;
    storage_save_config();

    ui_detail_refresh_manual();
    ui_detail_update_value();

    if (detail_crop < (int)tile_widgets.size()) {
        lv_obj_t* badge = tile_widgets[detail_crop].manbadge[detail_param];
        if (badge) {
            if (man) lv_obj_clear_flag(badge, LV_OBJ_FLAG_HIDDEN);
            else     lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_pump_step_cb(lv_event_t* e) {
    if (detail_crop < 0 || detail_crop >= (int)crops.size()) return;

    Crop& pl = crops[detail_crop];
    if (!pl.manual[detail_param]) return;        // đang Auto thì bỏ qua

    lv_obj_t* sw      = lv_event_get_target(e);
    bool      checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if      (sw == sw_inc) pl.sw1[detail_param] = checked;
    else if (sw == sw_dec) pl.sw2[detail_param] = checked;

    manual_lock_ms[detail_param] = millis();
    cloud_push_now = true;
    storage_save_config();
}

//  ══════ 16. WIDGET DÙNG LẠI ══════
static lv_obj_t* ui_make_limit_box(lv_obj_t* parent, int target, lv_obj_t** out_box) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 56, 26);
    lv_obj_set_style_bg_color(box, COL_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_border_color(box, COL_BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(box, 6);
    lv_obj_add_event_cb(box, ui_limit_box_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)target);

    lv_obj_t* lbl = lv_label_create(box);
    lv_label_set_text(lbl, "--");
    lv_obj_set_style_text_font(lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);

    if (out_box) *out_box = box;
    return lbl;
}

// Switch bị KHOÁ phải XÁM HẲN — bản cũ chỉ giảm opacity nên nhìn vẫn xanh, tưởng bơm đang chạy.
// Phải khai style cho TỔ HỢP CHECKED|DISABLED; để 2 style rời thì LVGL tự chọn, tuỳ phiên bản.
static lv_obj_t* ui_make_switch(lv_obj_t* parent) {
    lv_obj_t* sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 38, 22);

    lv_obj_set_style_bg_color(sw, COL_DOT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COL_ON, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(sw, COL_BORDER,   LV_PART_MAIN      | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(sw, COL_BORDER,   LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(sw, COL_TEXT_DIM, LV_PART_KNOB      | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(sw,   LV_OPA_COVER, LV_PART_MAIN      | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(sw,   LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(sw,   LV_OPA_60,    LV_PART_KNOB      | LV_STATE_DISABLED);

    lv_obj_set_style_bg_color(sw, COL_BORDER,   LV_PART_MAIN      | LV_STATE_CHECKED | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(sw,   LV_OPA_COVER, LV_PART_MAIN      | LV_STATE_CHECKED | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(sw, COL_BORDER,   LV_PART_INDICATOR | LV_STATE_CHECKED | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(sw,   LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(sw, COL_TEXT_DIM, LV_PART_KNOB      | LV_STATE_CHECKED | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(sw,   LV_OPA_60,    LV_PART_KNOB      | LV_STATE_CHECKED | LV_STATE_DISABLED);
    return sw;
}

//  ══════ 17. BIỂU ĐỒ ══════
static void ui_chart_tick_cb(lv_timer_t* t) {
    if (detail_crop < 0 || !chart || !series) return;

    float ev;
    bool  have = param_eff(detail_crop, detail_param, &ev);

    if (have) lv_chart_set_next_value(chart, series, (int32_t)(ev * 10));
    else      lv_chart_set_next_value(chart, series, LV_CHART_POINT_NONE);

    if (chart_ph) {
        if (have) lv_obj_add_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_clear_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);
    }

    ui_detail_update_lines();
    ui_detail_update_value();
}

// Trục Y lưu số x10 (LVGL chart chỉ nhận int) -> chia lại lúc vẽ nhãn.
static void ui_chart_draw_cb(lv_event_t* e) {
    lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;

    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
        fmt_val(detail_param, (float)dsc->value / 10.0f, dsc->text, dsc->text_length);
    } else if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        int sec = dsc->value - 29;
        if (sec == 0) snprintf(dsc->text, dsc->text_length, "Now");
        else          snprintf(dsc->text, dsc->text_length, "%ds", sec);
    }
}

//  ══════ 18. MỞ / ĐÓNG MÀN ══════
static void ui_close_detail_cb(lv_event_t* e) {
    if (chart_timer) { lv_timer_del(chart_timer); chart_timer = NULL; }

    lv_scr_load(scr_main);
    lv_obj_del_async(scr_detail);

    scr_detail = NULL;
    chart = NULL; series = NULL; series_lo = NULL; series_hi = NULL;
    chart_ph = NULL; cur_val_lbl = NULL;
    lo_box = hi_box = lo_lbl = hi_lbl = NULL;
    sw_manual = sw_inc = sw_dec = lbl_manual = NULL;
}

static void ui_open_detail(int crop_idx, int param_idx) {
    detail_crop  = crop_idx;
    detail_param = param_idx;
    ParamMeta* meta = &param_meta[param_idx];

    if (scr_detail) { lv_obj_del(scr_detail); scr_detail = NULL; }
    scr_detail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_detail, COL_CANVAS, 0);
    lv_obj_set_style_pad_all(scr_detail, 0, 0);
    lv_obj_clear_flag(scr_detail, LV_OBJ_FLAG_SCROLLABLE);

    // ---- tiêu đề ----
    lv_obj_t* h_prefix = lv_label_create(scr_detail);
    lv_label_set_text(h_prefix, "Detail: ");
    lv_obj_set_style_text_font(h_prefix, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(h_prefix, COL_TEXT, 0);
    lv_obj_align(h_prefix, LV_ALIGN_TOP_LEFT, 10, 6);

    lv_obj_t* h_name = lv_label_create(scr_detail);
    char up[24];
    size_t n = 0;
    for (const char* p = meta->full_name; *p && n < sizeof(up) - 1; ++p, ++n)
        up[n] = (char)toupper((unsigned char)*p);
    up[n] = '\0';
    lv_label_set_text(h_name, up);
    lv_obj_set_style_text_font(h_name, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(h_name, meta->color, 0);
    lv_obj_align_to(h_name, h_prefix, LV_ALIGN_OUT_RIGHT_MID, 2, 0);

    lv_obj_t* close_btn = lv_obj_create(scr_detail);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 30, 26);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(close_btn, 6);
    lv_obj_add_event_cb(close_btn, ui_close_detail_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, COL_TEXT_DIM, 0);
    lv_obj_center(close_lbl);

    // ---- thẻ A: ngưỡng ----
    lv_obj_t* cardA = lv_obj_create(scr_detail);
    lv_obj_remove_style_all(cardA);
    lv_obj_set_size(cardA, 150, 64);
    lv_obj_set_pos(cardA, 6, 30);
    lv_obj_set_style_bg_color(cardA, COL_CARD, 0);
    lv_obj_set_style_bg_opa(cardA, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cardA, 12, 0);
    lv_obj_set_style_border_color(cardA, COL_BORDER, 0);
    lv_obj_set_style_border_width(cardA, 1, 0);
    lv_obj_set_style_pad_all(cardA, 0, 0);
    lv_obj_clear_flag(cardA, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hiT = lv_label_create(cardA);
    lv_label_set_text(hiT, "Upper (Hi)");
    lv_obj_set_style_text_font(hiT, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hiT, COL_TEXT, 0);
    lv_obj_align(hiT, LV_ALIGN_TOP_LEFT, 6, 9);
    hi_lbl = ui_make_limit_box(cardA, 1, &hi_box);
    lv_obj_align(hi_box, LV_ALIGN_TOP_RIGHT, -8, 4);

    lv_obj_t* loT = lv_label_create(cardA);
    lv_label_set_text(loT, "Lower (Lo)");
    lv_obj_set_style_text_font(loT, FONT_SMALL, 0);
    lv_obj_set_style_text_color(loT, COL_TEXT, 0);
    lv_obj_align(loT, LV_ALIGN_TOP_LEFT, 6, 39);
    lo_lbl = ui_make_limit_box(cardA, 0, &lo_box);
    lv_obj_align(lo_box, LV_ALIGN_TOP_RIGHT, -8, 34);

    // ---- thẻ B: chế độ tay ----
    lv_obj_t* cardB = lv_obj_create(scr_detail);
    lv_obj_remove_style_all(cardB);
    lv_obj_set_size(cardB, 150, 134);
    lv_obj_set_pos(cardB, 6, 100);
    lv_obj_set_style_bg_color(cardB, COL_CARD, 0);
    lv_obj_set_style_bg_opa(cardB, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cardB, 12, 0);
    lv_obj_set_style_border_color(cardB, COL_BORDER, 0);
    lv_obj_set_style_border_width(cardB, 1, 0);
    lv_obj_set_style_pad_all(cardB, 0, 0);
    lv_obj_clear_flag(cardB, LV_OBJ_FLAG_SCROLLABLE);

    lbl_manual = lv_label_create(cardB);
    lv_label_set_long_mode(lbl_manual, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_manual, 98);
    lv_label_set_text(lbl_manual, "Manual Mode");
    lv_obj_set_style_text_font(lbl_manual, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_manual, COL_TEXT, 0);
    lv_obj_align(lbl_manual, LV_ALIGN_TOP_LEFT, 6, 20);
    sw_manual = ui_make_switch(cardB);
    lv_obj_align(sw_manual, LV_ALIGN_TOP_RIGHT, -6, 16);
    lv_obj_add_event_cb(sw_manual, ui_manual_mode_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* divi = lv_obj_create(cardB);
    lv_obj_remove_style_all(divi);
    lv_obj_set_size(divi, 134, 1);
    lv_obj_align(divi, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_color(divi, COL_BORDER, 0);
    lv_obj_set_style_bg_opa(divi, LV_OPA_COVER, 0);

    lv_obj_t* iT = lv_label_create(cardB);
    lv_label_set_long_mode(iT, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(iT, 98);
    lv_label_set_text(iT, "Pump Up (+)");
    lv_obj_set_style_text_font(iT, FONT_SMALL, 0);
    lv_obj_set_style_text_color(iT, COL_TEXT, 0);
    lv_obj_align(iT, LV_ALIGN_TOP_LEFT, 6, 68);
    sw_inc = ui_make_switch(cardB);
    lv_obj_align(sw_inc, LV_ALIGN_TOP_RIGHT, -6, 64);
    lv_obj_add_event_cb(sw_inc, ui_pump_step_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* dT = lv_label_create(cardB);
    lv_label_set_long_mode(dT, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(dT, 98);
    lv_label_set_text(dT, "Pump Down (-)");
    lv_obj_set_style_text_font(dT, FONT_SMALL, 0);
    lv_obj_set_style_text_color(dT, COL_TEXT, 0);
    lv_obj_align(dT, LV_ALIGN_TOP_LEFT, 6, 104);
    sw_dec = ui_make_switch(cardB);
    lv_obj_align(sw_dec, LV_ALIGN_TOP_RIGHT, -6, 100);
    lv_obj_add_event_cb(sw_dec, ui_pump_step_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ---- biểu đồ ----
    lv_obj_t* chart_bg = lv_obj_create(scr_detail);
    lv_obj_remove_style_all(chart_bg);
    lv_obj_set_size(chart_bg, 152, 204);
    lv_obj_set_pos(chart_bg, 162, 30);
    lv_obj_set_style_bg_color(chart_bg, COL_CARD, 0);
    lv_obj_set_style_bg_opa(chart_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chart_bg, 12, 0);
    lv_obj_set_style_border_width(chart_bg, 1, 0);
    lv_obj_set_style_border_color(chart_bg, COL_BORDER, 0);
    lv_obj_clear_flag(chart_bg, LV_OBJ_FLAG_SCROLLABLE);

    chart = lv_chart_create(chart_bg);
    lv_obj_set_size(chart, 116, 182);
    lv_obj_align(chart, LV_ALIGN_LEFT_MID, 30, 0);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 30);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 3, 1, true, 28);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_color(chart, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(chart, FONT_SMALL, LV_PART_TICKS);
    lv_obj_set_style_text_color(chart, COL_TEXT_DIM, LV_PART_TICKS);
    lv_obj_add_event_cb(chart, ui_chart_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    series_lo = lv_chart_add_series(chart, COL_WARN,    LV_CHART_AXIS_PRIMARY_Y);
    series_hi = lv_chart_add_series(chart, COL_BAD,     LV_CHART_AXIS_PRIMARY_Y);
    series    = lv_chart_add_series(chart, meta->color, LV_CHART_AXIS_PRIMARY_Y);

    // Có số sẵn -> lấp đầy 30 điểm cho đường liền mạch; chưa có -> để trống hẳn.
    float init_v;
    if (param_eff(crop_idx, param_idx, &init_v)) {
        for (int i = 0; i < 30; i++)
            lv_chart_set_next_value(chart, series, (int32_t)(init_v * 10));
    } else {
        lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
    }
    ui_detail_update_lines();

    chart_ph = lv_label_create(chart_bg);
    lv_label_set_text(chart_ph, "No data yet...");
    lv_obj_set_style_text_font(chart_ph, FONT_SMALL, 0);
    lv_obj_set_style_text_color(chart_ph, COL_TEXT_DIM, 0);
    lv_obj_align(chart_ph, LV_ALIGN_CENTER, 12, 0);

    float tmp;
    if (param_eff(crop_idx, param_idx, &tmp)) lv_obj_add_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);

    cur_val_lbl = lv_label_create(chart_bg);
    lv_obj_set_style_text_font(cur_val_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(cur_val_lbl, meta->color, 0);
    lv_obj_set_style_bg_color(cur_val_lbl, COL_BG, 0);
    lv_obj_set_style_bg_opa(cur_val_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(cur_val_lbl, 6, 0);
    lv_obj_set_style_pad_ver(cur_val_lbl, 2, 0);
    lv_obj_set_style_radius(cur_val_lbl, 6, 0);
    lv_obj_align(cur_val_lbl, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    ui_detail_update_value();

    ui_detail_update_labels();
    ui_detail_refresh_manual();

    if (chart_timer) lv_timer_del(chart_timer);
    chart_timer = lv_timer_create(ui_chart_tick_cb, 1000, NULL);

    lv_scr_load(scr_detail);
}

//  ══════ 19. MODAL ĐỔI TÊN THIẾT BỊ ══════

static lv_obj_t* name_modal_bg = NULL;
static lv_obj_t* name_modal_ta = NULL;

static void ui_close_device_name_modal() {
    if (!name_modal_bg) return;
    lv_obj_del(name_modal_bg);
    name_modal_bg = NULL;
    name_modal_ta = NULL;
}

static void ui_confirm_device_name(lv_event_t* e) {
    const char* text = lv_textarea_get_text(name_modal_ta);
    if (text && text[0]) {
        strncpy(device_name, text, sizeof(device_name) - 1);
        device_name[sizeof(device_name) - 1] = '\0';
        if (lbl_device_name) lv_label_set_text(lbl_device_name, device_name);

        name_lock_ms   = millis();   // khoá 5s: lệnh web tới sau không giành lại tên cũ
        cloud_push_now = true;
        storage_save_config();
    }
    ui_close_device_name_modal();
}

static void ui_device_name_kb_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if      (code == LV_EVENT_READY)  ui_confirm_device_name(e);
    else if (code == LV_EVENT_CANCEL) ui_close_device_name_modal();
}

static void ui_open_device_name_modal() {
    name_modal_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(name_modal_bg, 320, 240);
    lv_obj_set_style_bg_color(name_modal_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(name_modal_bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(name_modal_bg, 0, 0);
    lv_obj_clear_flag(name_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(name_modal_bg, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_t* card = lv_obj_create(name_modal_bg);
    lv_obj_set_size(card, 280, 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, COL_CANVAS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, "Device name:");
    lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT_DIM, 0);
    lv_obj_set_pos(lbl, 12, 6);

    name_modal_ta = lv_textarea_create(card);
    lv_textarea_set_one_line(name_modal_ta, true);
    lv_obj_clear_flag(name_modal_ta, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(name_modal_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(name_modal_ta, 6, 0);
    lv_obj_set_size(name_modal_ta, 256, 32);
    lv_obj_set_pos(name_modal_ta, 12, 24);
    lv_textarea_set_text(name_modal_ta, device_name);
    lv_textarea_set_max_length(name_modal_ta, DEVICE_NAME_MAXLEN - 1);

    lv_obj_t* kb = lv_keyboard_create(name_modal_bg);
    lv_obj_set_size(kb, 320, 134);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, name_modal_ta);
    lv_obj_add_event_cb(kb, ui_device_name_kb_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, ui_device_name_kb_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_add_state(name_modal_ta, LV_STATE_FOCUSED);
}

//  ══════ 20. MÀN NHẬP LIỆU (dùng chung cho SSID / mật khẩu / Chat ID) ══════
#define INPUT_SSID  0
#define INPUT_PASS  1
#define INPUT_TG    2

static lv_obj_t* set_bg        = NULL;   // màn Cài đặt
static lv_obj_t* set_status    = NULL;
static lv_obj_t* set_sim_lbl   = NULL;
static lv_obj_t* input_bg      = NULL;   // màn nhập, đè lên trên
static lv_obj_t* input_ta      = NULL;
static int       input_type    = 0;

static void ui_set_status(const char* msg, bool err) {
    if (!set_status) return;
    lv_label_set_text(set_status, msg);
    //  Báo lỗi thì tô đỏ, báo bình thường thì tô xám
    if (err) lv_obj_set_style_text_color(set_status, lv_color_hex(0xFF5555), 0);
    else     lv_obj_set_style_text_color(set_status, COL_TEXT_DIM, 0);
}

// Đóng màn nhập = LƯU ngay nội dung vừa gõ vào biến tương ứng.
static void ui_close_input() {
    if (input_ta) {
        const char* txt = lv_textarea_get_text(input_ta);

        if (input_type == INPUT_SSID) {
            strncpy(wifi_ssid, txt, sizeof(wifi_ssid) - 1);
            wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';

        } else if (input_type == INPUT_PASS) {
            strncpy(wifi_pass, txt, sizeof(wifi_pass) - 1);
            wifi_pass[sizeof(wifi_pass) - 1] = '\0';

        } else if (input_type == INPUT_TG) {
            strncpy(telegram_chat_id, txt, sizeof(telegram_chat_id) - 1);
            telegram_chat_id[sizeof(telegram_chat_id) - 1] = '\0';

            // Đổi Chat ID = đổi PHÒNG (topic MQTT + nhánh Firebase). Tính lại ngay.
            String room = deriveRoomId(String(telegram_chat_id));
            strncpy(mqtt_room_id, room.c_str(), sizeof(mqtt_room_id) - 1);
            mqtt_room_id[sizeof(mqtt_room_id) - 1] = '\0';

            storage_save_config();
            telegram_chat_changed = true;    // Telegram.h thấy cờ này thì đổi người nhận
        }
    }

    if (input_bg) {
        lv_obj_del(input_bg);
        input_bg = NULL;
        input_ta = NULL;
    }
    if (set_bg) lv_obj_clear_flag(set_bg, LV_OBJ_FLAG_HIDDEN);
}

static void ui_input_kb_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) ui_close_input();
}

static void ui_input_back_cb(lv_event_t* e) { ui_close_input(); }

static void ui_open_input(int type, const char* title, const char* default_val) {
    input_type = type;
    if (set_bg) lv_obj_add_flag(set_bg, LV_OBJ_FLAG_HIDDEN);

    input_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(input_bg, 320, 240);
    lv_obj_set_style_bg_color(input_bg, COL_BG, 0);
    lv_obj_set_style_border_width(input_bg, 0, 0);
    lv_obj_clear_flag(input_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_obj_create(input_bg);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 80, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 5);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, ui_input_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, COL_ACCENT, 0);
    lv_obj_align(back_lbl, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t* title_lbl = lv_label_create(input_bg);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, COL_TEXT_DIM, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 10);

    input_ta = lv_textarea_create(input_bg);
    lv_textarea_set_one_line(input_ta, true);
    lv_obj_set_size(input_ta, 280, 40);
    lv_obj_align(input_ta, LV_ALIGN_TOP_MID, 0, 45);
    lv_textarea_set_text(input_ta, default_val);

    lv_obj_t* kb = lv_keyboard_create(input_bg);
    lv_obj_set_size(kb, 320, 140);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, input_ta);
    lv_obj_add_event_cb(kb, ui_input_kb_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, ui_input_kb_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_add_state(input_ta, LV_STATE_FOCUSED);
}

static void ui_btn_ssid_cb(lv_event_t* e) { ui_open_input(INPUT_SSID, "Enter WiFi SSID", wifi_ssid); }
static void ui_btn_pass_cb(lv_event_t* e) { ui_open_input(INPUT_PASS, "Enter WiFi Password", wifi_pass); }
static void ui_btn_tg_cb(lv_event_t* e)   { ui_open_input(INPUT_TG,   "Enter Telegram Chat ID", telegram_chat_id); }

//  ══════ 21. MÀN CÀI ĐẶT ══════
static void ui_close_settings() {
    if (set_bg)   { lv_obj_del(set_bg);   set_bg = NULL; }
    if (input_bg) { lv_obj_del(input_bg); input_bg = NULL; input_ta = NULL; }
    set_status = NULL;
    set_sim_lbl = NULL;
}

static void ui_settings_back_cb(lv_event_t* e) { ui_close_settings(); }

static void ui_sim_sw_cb(lv_event_t* e) {
    //  Gạt công tắc: bật là chuyển sang mô phỏng, tắt là quay về đọc cảm biến thật
    bool sim = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (sim) sensor_set_source(SOURCE_SIM);
    else     sensor_set_source(SOURCE_RADIO);

    if (set_sim_lbl) {
        if (sim) {
            lv_label_set_text(set_sim_lbl, "Source: Simulated");
            lv_obj_set_style_text_color(set_sim_lbl, COL_ACCENT, 0);
        } else {
            lv_label_set_text(set_sim_lbl, "Source: ESP-NOW (real)");
            lv_obj_set_style_text_color(set_sim_lbl, COL_TEXT, 0);
        }
    }
}

// Thử nối ngay tại chỗ, xanh mới lưu. Reboot vì task MQTT/Telegram/Firebase dựng lúc boot.
static void ui_settings_save_cb(lv_event_t* e) {
    if (strlen(wifi_ssid) == 0) {
        ui_set_status("WiFi SSID not entered!", true);
        return;
    }

    ui_set_status("Connecting to WiFi...", false);
    lv_refr_now(NULL);

    if (!wifi_try_connect(8000)) {
        ui_set_status("WiFi Error! Check SSID/Password.", true);
        return;
    }

    storage_save_config();
    ui_set_status("OK! Saved - restarting...", false);
    lv_refr_now(NULL);
    delay(500);
    ESP.restart();
}

// Một dòng bấm-được trong thẻ cài đặt: [nhãn ......... giá trị]
static lv_obj_t* ui_settings_row(lv_obj_t* parent, const char* tag, const char* val,
                                 bool val_set, lv_coord_t y, lv_event_cb_t cb,
                                 lv_color_t val_col) {
    lv_obj_t* row = lv_btn_create(parent);
    lv_obj_set_size(row, 276, 30);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 12, y);
    lv_obj_set_style_bg_color(row, COL_BG, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* t = lv_label_create(row);
    lv_label_set_text(t, tag);
    lv_obj_set_style_text_font(t, FONT_SMALL, 0);
    lv_obj_set_style_text_color(t, COL_TEXT_DIM, 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t* v = lv_label_create(row);
    lv_label_set_text(v, val);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_width(v, 160);
    lv_obj_set_style_text_font(v, FONT_SMALL, 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    //  Có giá trị thì tô màu nhấn, chưa có (Not set) thì tô xám mờ
    if (val_set) lv_obj_set_style_text_color(v, val_col, 0);
    else         lv_obj_set_style_text_color(v, COL_TEXT_DIM, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -10, 0);

    return row;
}

static lv_obj_t* ui_settings_card(lv_obj_t* parent, lv_coord_t y, lv_coord_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, h);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, COL_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void ui_open_settings() {
    set_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(set_bg, 320, 240);
    lv_obj_set_style_bg_color(set_bg, COL_BG, 0);
    lv_obj_set_style_bg_opa(set_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(set_bg, 0, 0);
    lv_obj_set_style_pad_all(set_bg, 0, 0);
    lv_obj_set_style_radius(set_bg, 0, 0);
    lv_obj_clear_flag(set_bg, LV_OBJ_FLAG_SCROLLABLE);

    // ---- thanh tiêu đề ----
    lv_obj_t* bar = lv_obj_create(set_bg);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 320, 38);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_obj_create(bar);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 44, 34);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back_btn, 6);
    lv_obj_add_event_cb(back_btn, ui_settings_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_ic = lv_label_create(back_btn);
    lv_label_set_text(back_ic, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_ic, COL_ACCENT, 0);
    lv_obj_center(back_ic);

    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* save_btn = lv_btn_create(bar);
    lv_obj_set_size(save_btn, 78, 30);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_color(save_btn, COL_ACCENT, 0);
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);
    lv_obj_add_event_cb(save_btn, ui_settings_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_WIFI " Connect");
    lv_obj_set_style_text_font(save_lbl, FONT_SMALL, 0);
    lv_obj_center(save_lbl);

    // ---- thẻ WiFi ----
    lv_obj_t* cardW = ui_settings_card(set_bg, 42, 96);

    lv_obj_t* wHead = lv_label_create(cardW);
    lv_label_set_text(wHead, "WIFI NETWORK");
    lv_obj_set_style_text_font(wHead, FONT_SMALL, 0);
    lv_obj_set_style_text_color(wHead, COL_TEXT_DIM, 0);
    lv_obj_align(wHead, LV_ALIGN_TOP_LEFT, 12, 8);

    //  Dòng SSID: có thì hiện tên WiFi, chưa có thì hiện "Not set"
    const char* ssid_txt;
    if (strlen(wifi_ssid)) ssid_txt = wifi_ssid;
    else                   ssid_txt = "Not set";
    ui_settings_row(cardW, "SSID", ssid_txt,
                    strlen(wifi_ssid) > 0, 28, ui_btn_ssid_cb, COL_TEXT);

    char dots[17];
    size_t n = strlen(wifi_pass);
    if (n > 12) n = 12;
    for (size_t k = 0; k < n; k++) dots[k] = '*';
    dots[n] = '\0';
    //  Dòng mật khẩu: có thì hiện dấu sao, chưa có thì hiện "Not set"
    const char* pass_txt;
    if (n) pass_txt = dots;
    else   pass_txt = "Not set";
    ui_settings_row(cardW, "Password", pass_txt,
                    n > 0, 62, ui_btn_pass_cb, COL_TEXT);

    // ---- thẻ Telegram ----
    lv_obj_t* cardT = ui_settings_card(set_bg, 142, 46);
    //  Dòng Telegram ID: có thì hiện ID, chưa có thì báo tắt cảnh báo
    const char* tg_txt;
    if (strlen(telegram_chat_id)) tg_txt = telegram_chat_id;
    else                          tg_txt = "Not set (alerts off)";
    ui_settings_row(cardT, LV_SYMBOL_BELL " Telegram ID", tg_txt,
                    strlen(telegram_chat_id) > 0, 8, ui_btn_tg_cb, COL_ON);

    // ---- thẻ nguồn số ----
    lv_obj_t* cardS = ui_settings_card(set_bg, 192, 48);
    bool sim = (sensor_source == SOURCE_SIM);

    set_sim_lbl = lv_label_create(cardS);
    //  Hiện nhãn nguồn số theo trạng thái hiện tại
    if (sim) {
        lv_label_set_text(set_sim_lbl, "Source: Simulated");
        lv_obj_set_style_text_color(set_sim_lbl, COL_ACCENT, 0);
    } else {
        lv_label_set_text(set_sim_lbl, "Source: ESP-NOW (real)");
        lv_obj_set_style_text_color(set_sim_lbl, COL_TEXT, 0);
    }
    lv_obj_set_style_text_font(set_sim_lbl, FONT_SMALL, 0);
    lv_obj_align(set_sim_lbl, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t* sim_sw = lv_switch_create(cardS);
    lv_obj_set_size(sim_sw, 48, 26);
    lv_obj_align(sim_sw, LV_ALIGN_RIGHT_MID, -12, 0);
    if (sim) lv_obj_add_state(sim_sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sim_sw, COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sim_sw, ui_sim_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ---- dòng trạng thái ----
    set_status = lv_label_create(set_bg);
    lv_label_set_text(set_status, "");
    lv_obj_set_style_text_font(set_status, FONT_SMALL, 0);
    lv_obj_set_style_text_color(set_status, COL_TEXT_DIM, 0);
    lv_obj_align(set_status, LV_ALIGN_BOTTOM_MID, 0, -8);
}


//  ══════ 22. KHỞI TẠO ══════

// ---------------- Khởi tạo ----------------
// Cấu hình PHẢI được nạp trước (setup() trong .ino lo việc đó).
static void ui_init() {
    for (int i = 0; i < P_COUNT; i++) sensor_valid[i] = false;
    sensor_last_rx_ms = 0;

    if (crops.empty()) {
        Crop lettuce;
        memset(&lettuce, 0, sizeof(lettuce));
        strcpy(lettuce.name, "Lettuce Base");
        lettuce.logo_idx = 0;
        lettuce.logo     = CROP_LOGOS[0];
        for (int i = 0; i < P_COUNT; i++) {
            lettuce.setpoint[i] = LETTUCE_SETPOINT[i];
            lettuce.value[i]    = lettuce.setpoint[i];
            lettuce.lo_limit[i] = clamp_phys(i, param_meta[i].lo_limit);
            lettuce.hi_limit[i] = clamp_phys(i, param_meta[i].hi_limit);
        }
        crops.push_back(lettuce);
        active_crop  = 0;
        current_page = 0;
        storage_save_config();
    }

    if (sensor_source == SOURCE_SIM) sim_seed();

    ui_build_alarm_overlay();
    ui_build_main_screen();
    ui_goto_page(current_page, false);
    lv_scr_load(scr_main);

    lv_timer_create(ui_header_status_cb, 1000, NULL);
    lv_timer_create(ui_refresh_cb,       1000, NULL);
    lv_timer_create(ui_alarm_blink_cb,   500,  NULL);
}

// ================= LỆNH TỪ WEB =================
// KHÔNG tin web: kẹp vật lý ngay tại cửa, và bỏ qua lệnh nếu vừa chỉnh tay trên màn (khoá 5s).

//  ══════ 23. CỬA NHẬN LỆNH TỪ WEB ══════

static void ui_apply_remote_name(const char* name) {
    if (locked(name_lock_ms)) return;
    if (!name || !name[0]) return;
    if (strncmp(name, device_name, sizeof(device_name) - 1) == 0) return;

    strncpy(device_name, name, sizeof(device_name) - 1);
    device_name[sizeof(device_name) - 1] = '\0';
    if (lbl_device_name) lv_label_set_text(lbl_device_name, device_name);
    storage_save_config();
}

static void ui_apply_remote_limits(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    if (active_crop < 0 || active_crop >= (int)crops.size()) return;

    Crop& pl = crops[active_crop];
    bool changed = false;

    for (int i = 0; i < P_COUNT; i++) {
        JsonVariant o = doc[SENSOR_KEYS[i]];
        if (o.isNull()) continue;

        if (!locked(limit_lock_ms[i])) {
            if (!o["lo"].isNull()) {
                float v = clamp_phys(i, o["lo"].as<float>());
                if (v > pl.hi_limit[i]) v = pl.hi_limit[i];      // ép lo <= hi
                if (fabsf(v - pl.lo_limit[i]) > 0.001f) { pl.lo_limit[i] = v; changed = true; }
            }
            if (!o["hi"].isNull()) {
                float v = clamp_phys(i, o["hi"].as<float>());
                if (v < pl.lo_limit[i]) v = pl.lo_limit[i];
                if (fabsf(v - pl.hi_limit[i]) > 0.001f) { pl.hi_limit[i] = v; changed = true; }
            }
        }

        if (!locked(manual_lock_ms[i])) {
            if (!o["man"].isNull()) {
                bool m = o["man"].as<bool>();
                if (m != pl.manual[i]) { pl.manual[i] = m; changed = true; }
                if (m && !o["mv"].isNull()) {
                    float mv = clampf(o["mv"].as<float>(), param_meta[i].min_v, param_meta[i].max_v);
                    if (fabsf(mv - pl.value[i]) > 0.001f) { pl.value[i] = mv; changed = true; }
                    if (!pl.manual_val_set[i])            { pl.manual_val_set[i] = true; changed = true; }
                }
            }
            if (!o["sw1"].isNull()) {
                bool v = o["sw1"].as<bool>();
                if (v != pl.sw1[i]) { pl.sw1[i] = v; changed = true; }
            }
            if (!o["sw2"].isNull()) {
                bool v = o["sw2"].as<bool>();
                if (v != pl.sw2[i]) { pl.sw2[i] = v; changed = true; }
            }
        }
    }

    if (!changed) return;
    storage_save_config();
    ui_detail_refresh_limits();
}

// Lệnh bơm chỉ cần "sw". Bản cũ đòi thêm "man" mà web không bao giờ gửi -> bơm từ web chết.
static void ui_apply_remote_pump(const char* json) {
    if (locked(pump_lock_ms)) return;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    if (doc["sw"].isNull()) return;

    bool s = doc["sw"].as<bool>();
    if (s == pump_on) return;

    pump_on = s;
    ui_pump_sync_switches();
    storage_save_config();
}
