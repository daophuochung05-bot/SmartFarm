#pragma once

// Cấu hình màn hình CYD gồm linh kiện TFT, chip cảm ứng và thư viện đồ họa LVGL
// Màn hình TFT chạy bus HSPI còn chip cảm ứng chạy bus VSPI độc lập hoàn toàn
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Thông số độ phân giải vật lý của màn hình
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

// Sơ đồ chân kết nối phần cứng của chip cảm ứng và đèn nền
#define TOUCH_PIN_IRQ   36      // Chân phát tín hiệu ngắt khi có lực chạm
#define TOUCH_PIN_MOSI  32      // Chân truyền dữ liệu SPI Master Out Slave In
#define TOUCH_PIN_MISO  39      // Chân nhận dữ liệu SPI Master In Slave Out
#define TOUCH_PIN_CLK   25      // Chân cấp xung nhịp giữ nhịp SPI
#define TOUCH_PIN_CS    33      // Chân lựa chọn chip cảm ứng trên bus SPI
#define BACKLIGHT_PIN   21      // Chân cấp nguồn điều khiển độ sáng đèn nền

// Tọa độ thô đọc từ IC cảm ứng XPT2046 dùng để cấu hình hiệu chuẩn màn hình
#define TOUCH_RAW_X_MIN  280
#define TOUCH_RAW_X_MAX  3800
#define TOUCH_RAW_Y_MIN  340
#define TOUCH_RAW_Y_MAX  3800

#define DRAW_BUFFER_LINES  28   // Số lượng dòng quét phân tách cho bộ đệm đồ họa tốn 17.9KB RAM

// Lớp cấu hình quản lý phần cứng hiển thị kết hợp đọc dữ liệu cảm ứng qua LVGL
class Screen {
public:
    Screen();
    void begin(bool backlight = true, uint8_t rotation = 3, bool serialLog = false);
    void update();                            // Hàm duy trì chu kỳ quét tọa độ chạm và cập nhật đồ họa

    // Điều khiển trạng thái đóng ngắt cấp nguồn cho hệ thống đèn nền màn hình
    void setBacklight(bool on) {
        if (on) digitalWrite(BACKLIGHT_PIN, HIGH);
        else    digitalWrite(BACKLIGHT_PIN, LOW);
    }

    SPIClass& touchSPI() { return _touchSPI; } // Chia sẻ quyền truy cập bus cảm ứng phục vụ giao tiếp thẻ SD

private:
    TFT_eSPI            _tft;                // Thực thể điều khiển IC hiển thị màn hình TFT
    SPIClass            _touchSPI;           // Kênh SPI độc lập dùng riêng cho chip cảm ứng
    XPT2046_Touchscreen _touch;              // Thực thể xử lý tín hiệu IC cảm ứng

    lv_color_t          _buf[SCREEN_WIDTH * DRAW_BUFFER_LINES];   // Khối bộ đệm pixel dựng hình của LVGL
    lv_disp_draw_buf_t  _drawBuf;
    lv_disp_drv_t       _dispDrv;            // Cấu hình trình điều khiển xuất tín hiệu hiển thị
    lv_indev_drv_t      _indevDrv;           // Cấu hình trình điều khiển nhận tín hiệu tương tác chạm

    uint32_t _lastTick;                      // Lưu mốc thời gian của chu kỳ xử lý kế trước
    bool     _serialLog;
    uint8_t  _rotation;

    void _rawToPixel(uint16_t rx, uint16_t ry, uint16_t& px, uint16_t& py);      // Đổi hệ tọa độ thô sang pixel
    static void _flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px);   // Hàm callback xuất pixel ra màn TFT
    static void _touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data);            // Hàm callback nạp tọa độ nhấn vào LVGL
};

static Screen   screen;                      // Thực thể quản lý màn hình duy nhất trong hệ thống
static Screen* screen_self = nullptr;        // Con trỏ toàn cục giúp các hàm liên kết tĩnh truy cập bộ nhớ lớp

// Khởi tạo kênh truyền SPI gán cố định cổng VSPI và cấu hình chân chọn chip cảm ứng
inline Screen::Screen()
    : _touchSPI(VSPI)
    , _touch(TOUCH_PIN_CS, TOUCH_PIN_IRQ)
    , _lastTick(0), _serialLog(false), _rotation(3)
{
    screen_self = this;
}

// Thiết lập trạng thái ban đầu cho màn hình TFT, cấu hình chiều quay và đăng ký driver hiển thị
inline void Screen::begin(bool backlight, uint8_t rotation, bool serialLog) {
    _serialLog = serialLog;
    _rotation  = rotation;

    pinMode(BACKLIGHT_PIN, OUTPUT);
    if (backlight) digitalWrite(BACKLIGHT_PIN, HIGH);
    else           digitalWrite(BACKLIGHT_PIN, LOW);

    _tft.init();
    _tft.setRotation(_rotation);
    _tft.fillScreen(TFT_BLACK);

    _touchSPI.begin(TOUCH_PIN_CLK, TOUCH_PIN_MISO, TOUCH_PIN_MOSI, TOUCH_PIN_CS);
    _touch.begin(_touchSPI);
    _touch.setRotation(_rotation);

    lv_init();
    lv_disp_draw_buf_init(&_drawBuf, _buf, NULL, SCREEN_WIDTH * DRAW_BUFFER_LINES);

    lv_disp_drv_init(&_dispDrv);
    _dispDrv.hor_res  = SCREEN_WIDTH;
    _dispDrv.ver_res  = SCREEN_HEIGHT;
    _dispDrv.flush_cb = _flushCb;
    _dispDrv.draw_buf = &_drawBuf;
    lv_disp_drv_register(&_dispDrv);

    lv_indev_drv_init(&_indevDrv);
    _indevDrv.type    = LV_INDEV_TYPE_POINTER;
    _indevDrv.read_cb = _touchCb;
    lv_indev_drv_register(&_indevDrv);

    _lastTick = millis();
    if (_serialLog) Serial.println("[Screen] TFT + Touch + LVGL san sang");
}

// Tính toán thời gian chênh lệch để duy trì bộ đếm thời gian nội tại của thư viện đồ họa
inline void Screen::update() {
    uint32_t now = millis();
    lv_tick_inc(now - _lastTick);   // Đồng bộ thời gian thực tế đã trôi qua vào nhân đồ họa
    _lastTick = now;
    lv_timer_handler();             // Lệnh ép thực thi các tác vụ vẽ giao diện và kiểm tra cảm ứng
}

// Chuyển đổi dải giá trị ADC thô của chip cảm ứng về đúng giới hạn pixel màn hình
inline void Screen::_rawToPixel(uint16_t rx, uint16_t ry, uint16_t& px, uint16_t& py) {
    px = (uint16_t)map(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, SCREEN_WIDTH  - 1);
    py = (uint16_t)map(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, SCREEN_HEIGHT - 1);
    px = constrain(px, 0, SCREEN_WIDTH  - 1);
    py = constrain(py, 0, SCREEN_HEIGHT - 1);
}

// Đẩy dữ liệu màu từ bộ đệm RAM của LVGL vào vùng nhớ hiển thị của màn hình TFT
inline void Screen::_flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    if (!screen_self) { lv_disp_flush_ready(drv); return; }

    TFT_eSPI& tft = screen_self->_tft;
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&px->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(drv);               // Gửi tín hiệu hoàn thành tác vụ vẽ để giải phóng bộ đệm
}

// Đọc trạng thái nút nhấn từ phần cứng để cập nhật tọa độ pixel vào cấu hình đầu vào LVGL
inline void Screen::_touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (!screen_self || !screen_self->_touch.touched()) {
        if (data) data->state = LV_INDEV_STATE_REL; // Báo trạng thái nhả tay khi không phát hiện lực nhấn
        return;
    }

    TS_Point p = screen_self->_touch.getPoint();
    uint16_t px, py;
    screen_self->_rawToPixel(p.x, p.y, px, py);

    data->state   = LV_INDEV_STATE_PR;      // Báo trạng thái đang nhấn giữ kèm tọa độ pixel tương ứng
    data->point.x = (lv_coord_t)px;
    data->point.y = (lv_coord_t)py;
}