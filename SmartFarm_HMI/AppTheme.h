#pragma once
//  Theme — Bảng màu, phông chữ, kích thước dùng chung cho toàn bộ giao diện.
//  Tách riêng ra đây để cả màn hình chính (AppUI.h) lẫn popup ghép cặp (AppSensor.h) cùng xài.
#include <lvgl.h>

//  Phông chữ (dùng font Montserrat có sẵn của LVGL)
#define FONT_TITLE     &lv_font_montserrat_14   //  Tiêu đề
#define FONT_NORMAL    &lv_font_montserrat_14   //  Chữ thường
#define FONT_SMALL     &lv_font_montserrat_12   //  Chữ nhỏ (ghi chú, đơn vị)
#define FONT_VALUE     &lv_font_montserrat_18   //  Số đo (to, dễ nhìn)

//  Màu nền và khung
#define COL_BG         lv_color_hex(0x0B0C10)   //  Nền ngoài cùng (đen)
#define COL_CANVAS     lv_color_hex(0x14161D)   //  Nền ô nhỏ
#define COL_CARD       lv_color_hex(0x1D212A)   //  Nền thẻ
#define COL_BORDER     lv_color_hex(0x2A2C33)   //  Viền
#define COL_ACCENT     lv_color_hex(0x007AFF)   //  Màu nhấn (xanh dương)

//  Màu chữ
#define COL_TEXT       lv_color_hex(0xF5F5F7)   //  Chữ chính (trắng)
#define COL_TEXT_DIM   lv_color_hex(0x86868B)   //  Chữ mờ (xám)
#define COL_DOT        lv_color_hex(0x48484A)   //  Chấm trang chưa chọn

//  Màu trạng thái
#define COL_ON         lv_color_hex(0x34C759)   //  Xanh lá: bật / ổn
#define COL_WARN       lv_color_hex(0xFF9F0A)   //  Cam: cảnh báo
#define COL_BAD        lv_color_hex(0xFF453A)   //  Đỏ: vượt ngưỡng
#define COL_OFF        lv_color_hex(0xFF3B30)   //  Đỏ: tắt / mất kết nối

//  Kích thước bố cục màn hình 320x240
#define HEADER_H   20                           //  Chiều cao thanh tiêu đề
#define DOTS_H     12                           //  Chiều cao hàng chấm trang
#define TILE_H     (240 - HEADER_H - DOTS_H)    //  Chiều cao vùng nội dung còn lại
