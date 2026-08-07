#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>
#include "AppData.h"
#include "AppTheme.h"

#define SENSOR_TIMEOUT_MS  5000     // Thời gian tối đa để xác định mất tín hiệu cảm biến

#define RADIO_MAX_PAYLOAD  250      // Giới hạn dung lượng gói tin cấu hình mặc định của ESP NOW
#define RADIO_QUEUE_CAP    6
#define PAIR_TIMEOUT_MS    30000    // Giới hạn thời gian chờ xác nhận ghép cặp thiết bị
#define PAIR_COOLDOWN_MS   30000    // Thời gian chặn yêu cầu kết nối lặp lại để tránh xung đột

#define SIM_PULL   0.02f            // Hệ số kéo giá trị mô phỏng về điểm đặt mong muốn
#define SIM_STEP   0.015f           // Biên độ dao động ngẫu nhiên của thuật toán giả lập

// 1. GIAO TIẾP VÔ TUYẾN ESP NOW
    class SensorRadio {
    public:
        bool begin(uint8_t fallbackChannel = 1);     // Khởi chạy sau khi cấu hình WiFi hoàn tất
        bool started() const { return _started; }

        // Các hàm tương tác thuộc luồng xử lý đồ họa LVGL
        bool popLine(String& out);
        void beacon();                               // Phát quảng bá tín hiệu beacon định kỳ
        bool sendLine(const String& json);

        bool     pendingPair(uint8_t out[6]);
        uint32_t pairRemainMs();
        void     acceptPair();
        void     rejectPair();
        bool     restorePeer(const char* macStr);    // Khôi phục địa chỉ MAC đã lưu trong bộ nhớ
        void     forgetPeer();

        // Bắn nốt các gói {"pa"} còn lại của lượt accept gần nhất, KHÔNG chặn (gọi từ lv_timer, luồng LVGL).
        // Thay cho vòng lặp delay() cũ vốn chạy ngay trong callback ESP-NOW rất dễ làm nghẽn driver WiFi.
        void     pumpResendAccept();

        bool     hasPeer() const { return _hasPeer; }
        String   peerMacStr() const;
        uint8_t  channel();                          // Đọc kênh sóng hiện tại từ cấu hình WiFi
        uint32_t rxCount() const { return _rx; }

        void onRecv(const uint8_t* mac, const uint8_t* data, int len);   // Thực thi trên luồng WiFi

    private:
        volatile bool _started = false;
        volatile bool _hasPeer = false;
        uint8_t       _peer[6] = {0};

        volatile bool _pendReq    = false;
        uint8_t       _pendMac[6] = {0};
        uint32_t      _pendSince  = 0;
        uint8_t       _banMac[6]  = {0};
        uint32_t      _banUntil   = 0;

        uint8_t           _channel = 0;
        volatile uint32_t _rx      = 0;

        // Trạng thái gửi lặp {"pa"} (chống mất gói ghép) — xử lý ngoài callback ESP-NOW
        bool     _resendAccept = false;
        uint8_t  _resendMac[6] = {0};
        uint8_t  _resendLeft   = 0;
        uint32_t _resendLast   = 0;

        std::vector<String> _queue;
        SemaphoreHandle_t   _mutex = nullptr;

        bool _addPeer(const uint8_t* mac);
        void _sendAccept(const uint8_t* mac);
    };

    static SensorRadio  sensorRadio;
    static SensorRadio* radio_self = nullptr;
    static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    // Tính năng: Callback trung gian tiếp nhận dữ liệu thô từ phần cứng ESP-NOW tùy theo phiên bản Arduino Core
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
    inline void radio_recv_cb(const esp_now_recv_info_t* info, const uint8_t* d, int n) {
        if (radio_self && info) radio_self->onRecv(info->src_addr, d, n);
    }
    #else
    inline void radio_recv_cb(const uint8_t* mac, const uint8_t* d, int n) {
        if (radio_self) radio_self->onRecv(mac, d, n);
    }
    #endif

    // Tính năng: Khởi tạo cấu hình mạng không dây, đặt kênh sóng và kích hoạt tính năng ESP-NOW cho CYD
    inline bool SensorRadio::begin(uint8_t fallbackChannel) {
        if (_started) return true;

        radio_self = this;
        if (!_mutex) _mutex = xSemaphoreCreateMutex();

        WiFi.mode(WIFI_STA);                // Chuyển chế độ WiFi Station trước khi khởi tạo ESP NOW

        if (WiFi.status() == WL_CONNECTED) {
            _channel = WiFi.channel();          // Cấu hình kênh theo bộ định tuyến kết nối ổn định
            Serial.printf("[Sensor] Bam kenh router: CH %u\n", _channel);
        } else {
            esp_wifi_set_promiscuous(true);
            esp_wifi_set_channel(fallbackChannel, WIFI_SECOND_CHAN_NONE);
            esp_wifi_set_promiscuous(false);
            _channel = fallbackChannel;
            Serial.printf("[Sensor] Offline -> khoa cung CH %u\n", _channel);
        }

        WiFi.setSleep(false);                   // Hủy kích hoạt chế độ tiết kiệm năng lượng

        if (esp_now_init() != ESP_OK) {
            Serial.println("[Sensor] esp_now_init THAT BAI");
            return false;
        }
        esp_now_register_recv_cb(radio_recv_cb);
        _addPeer(BROADCAST_MAC);                // Đăng ký địa chỉ quảng bá bắt buộc để gửi gói tin định kỳ

        _started = true;
        Serial.printf("[Sensor] San sang — MAC cua CYD: %s\n", WiFi.macAddress().c_str());
        return true;
    }

    // Tính năng: Đăng ký địa chỉ MAC của thiết bị đích vào danh sách liên kết truyền thông của bộ nhớ cứng ESP-NOW
    inline bool SensorRadio::_addPeer(const uint8_t* mac) {
        if (esp_now_is_peer_exist(mac)) return true;
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0;        // Giá trị 0 cấu hình thiết bị tự động bám theo kênh hiện tại
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = false;
        return esp_now_add_peer(&peer) == ESP_OK;
    }

    // Tính năng: Hủy đăng ký và xóa thông tin địa chỉ MAC của trạm cảm biến hiện tại ra khỏi hệ thống
    inline void SensorRadio::forgetPeer() {
        if (!_hasPeer) return;
        esp_now_del_peer(_peer);
        _hasPeer = false;
        Serial.println("[Sensor] Da quen peer -> san sang ghep con khac.");
    }

    // Tính năng: Định dạng mảng 6 byte địa chỉ MAC vật lý thành dạng chuỗi văn bản (String) để hiển thị lên giao diện
    inline String SensorRadio::peerMacStr() const {
        if (!_hasPeer) return "--";
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                _peer[0], _peer[1], _peer[2], _peer[3], _peer[4], _peer[5]);
        return String(buf);
    }

    // Tính năng: Xử lý ngắt nhận gói tin vô tuyến, phân loại dữ liệu, lọc nhiễu MAC và đẩy dữ liệu vào hàng đợi an toàn
    inline void SensorRadio::onRecv(const uint8_t* mac, const uint8_t* data, int len) {
        if (len <= 0 || len > RADIO_MAX_PAYLOAD) return;
        if (data[0] != '{') return;

        // Fast-path: Loại bỏ gói tin beacon của các mạch hiển thị khác trong khu vực quét
        if (len >= 5 && memcmp(data, "{\"hb\"", 5) == 0) return;

        // Kiểm tra gói tin yêu cầu ghép cặp thiết bị mới (Pair Request)
        if (len >= 5 && memcmp(data, "{\"pr\"", 5) == 0) {
            if (_hasPeer) {
                // Tự động chấp nhận kết nối lại nếu trùng địa chỉ MAC đã xác thực từ trước
                if (memcmp(_peer, mac, 6) == 0) _sendAccept(mac);
                return;
            }
            if (millis() < _banUntil && memcmp(_banMac, mac, 6) == 0) return;
            if (_pendReq) return;

            memcpy(_pendMac, mac, 6);
            _pendSince = millis();
            _pendReq   = true;      // Kích hoạt cờ yêu cầu hiển thị hộp thoại xác nhận trên giao diện
            return;
        }

        // Lọc và chỉ xử lý các gói tin dữ liệu cảm biến gửi từ thiết bị đã liên kết thành công
        if (!_hasPeer || memcmp(_peer, mac, 6) != 0) return;

        String line;
        line.reserve(len + 1);
        for (int i = 0; i < len; i++) line += (char)data[i];

        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (_queue.size() >= RADIO_QUEUE_CAP) _queue.erase(_queue.begin());  // Xóa bớt phần tử cũ nếu hàng đợi đầy
            _queue.push_back(line);
            xSemaphoreGive(_mutex);
            _rx++;
        }
    }

    // Tính năng: Kiểm tra trạng thái yêu cầu ghép cặp đang chờ và thực hiện timeout nếu quá thời gian quy định
    inline bool SensorRadio::pendingPair(uint8_t out[6]) {
        if (!_pendReq) return false;
        if (millis() - _pendSince > PAIR_TIMEOUT_MS) {
            Serial.println("[Sensor] Het gio khong ai bam -> tu choi ghep.");
            rejectPair();
            return false;
        }
        memcpy(out, _pendMac, 6);
        return true;
    }

    // Tính năng: Tính toán thời gian đếm ngược còn lại (đơn vị mili-giây) của chu kỳ chờ người dùng bấm xác nhận ghép cặp
    inline uint32_t SensorRadio::pairRemainMs() {
        if (!_pendReq) return 0;
        uint32_t elapsed = millis() - _pendSince;
        if (elapsed >= PAIR_TIMEOUT_MS) return 0;      // Đã hết thời gian cho phép
        return PAIR_TIMEOUT_MS - elapsed;
    }

    // Tính năng: Phát gói tin chấp nhận liên kết vô tuyến ra không gian gửi trực tiếp đến địa chỉ MAC yêu cầu
    //  QUAN TRỌNG: hàm này có thể được gọi từ NGAY TRONG callback nhận ESP-NOW (case auto-reconnect ở onRecv()),
    //  tức đang chạy trên task driver WiFi. TUYỆT ĐỐI không delay()/block ở đây — chặn task đó dù chỉ vài chục ms
    //  cũng đủ làm rớt gói/khựng sóng, đặc biệt lúc đang ghép lại dồn dập (đúng lúc sóng chập chờn nhất).
    //  Vẫn giữ đúng tính năng "gửi lặp 3 lần chống mất gói": gửi ngay 1 gói ở đây, 2 gói còn lại giao cho
    //  pumpResendAccept() bắn tiếp trên luồng LVGL (không delay, không chặn ai cả).
    inline void SensorRadio::_sendAccept(const uint8_t* mac) {
        _addPeer(mac);
        static const char ACCEPT[] = "{\"pa\":1}";
        esp_now_send(mac, (const uint8_t*)ACCEPT, sizeof(ACCEPT) - 1);   // gửi ngay lần 1

        memcpy(_resendMac, mac, 6);
        _resendLeft   = 2;         // còn 2 lần gửi lặp
        _resendLast   = millis();
        _resendAccept = true;
    }

    // Tính năng: Bắn nốt các gói {"pa"} còn lại của lượt accept gần nhất — gọi định kỳ từ lv_timer (luồng LVGL),
    // KHÔNG delay() nên không chặn gì cả. Giữ đúng khoảng cách ~20ms giữa các lần gửi như bản cũ.
    inline void SensorRadio::pumpResendAccept() {
        if (!_resendAccept) return;
        if (millis() - _resendLast < 20) return;

        static const char ACCEPT[] = "{\"pa\":1}";
        esp_now_send(_resendMac, (const uint8_t*)ACCEPT, sizeof(ACCEPT) - 1);
        _resendLast = millis();

        if (--_resendLeft == 0) _resendAccept = false;
    }

    // Tính năng: Xác thực chấp nhận yêu cầu kết nối, lưu địa chỉ MAC thiết bị cảm biến vào luồng làm việc chính
    inline void SensorRadio::acceptPair() {
        if (!_pendReq) return;
        memcpy(_peer, _pendMac, 6);
        _hasPeer  = true;
        _pendReq  = false;
        _banUntil = 0;
        _sendAccept(_peer);
        Serial.printf("[Sensor] DA GHEP: %s\n", peerMacStr().c_str());
    }

    // Tính năng: Từ chối yêu cầu ghép cặp và đưa địa chỉ MAC đó vào danh sách đen (Blacklist) tạm thời trong 30 giây
    inline void SensorRadio::rejectPair() {
        if (!_pendReq) return;
        memcpy(_banMac, _pendMac, 6);
        _banUntil = millis() + PAIR_COOLDOWN_MS;
        _pendReq  = false;
        Serial.println("[Sensor] Tu choi ghep -> cam MAC do 30s.");
    }

    // Tính năng: Phân tích cú pháp chuỗi văn bản MAC lưu trong bộ nhớ Flash (NVS) và khôi phục kết nối phần cứng cũ
    inline bool SensorRadio::restorePeer(const char* macStr) {
        if (!macStr || !macStr[0]) return false;

        uint8_t mac[6];
        int n = 0;
        for (const char* p = macStr; *p && n < 6; ) {
            if (*p == ':' || *p == '-') { p++; continue; }
            unsigned byteVal;
            if (sscanf(p, "%2x", &byteVal) != 1) return false;
            mac[n++] = (uint8_t)byteVal;
            p += 2;
        }
        if (n != 6) return false;

        memcpy(_peer, mac, 6);
        _hasPeer = _addPeer(_peer);
        if (_hasPeer) Serial.printf("[Sensor] Nap lai peer tu config: %s\n", peerMacStr().c_str());
        return _hasPeer;
    }

    // Tính năng: Lấy gói tin JSON lưu trữ đầu tiên ra khỏi hàng đợi an toàn (Pop Queue) để chuẩn bị bóc tách dữ liệu
    inline bool SensorRadio::popLine(String& out) {
        if (!_mutex) return false;
        bool got = false;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!_queue.empty()) {
                out = _queue.front();
                _queue.erase(_queue.begin());
                got = true;
            }
            xSemaphoreGive(_mutex);
        }
        return got;
    }

    // Tính năng: Đọc và trả về kênh sóng hiện tại của phần cứng WiFi đang hoạt động trên hệ thống
    inline uint8_t SensorRadio::channel() {
        if (WiFi.status() == WL_CONNECTED) _channel = WiFi.channel();
        return _channel;
    }

    // Tính năng: Phát quảng bá gói tin truyền thông beacon định kỳ ra môi trường để các trạm cảm biến xung quanh dò kênh
    inline void SensorRadio::beacon() {
        if (!_started) return;
        static const char HEARTBEAT[] = "{\"hb\":1}";
        esp_now_send(BROADCAST_MAC, (const uint8_t*)HEARTBEAT, sizeof(HEARTBEAT) - 1);
    }

    // Tính năng: Đóng gói và gửi một chuỗi định dạng JSON trực tiếp đến địa chỉ MAC của trạm cảm biến đã liên kết
    inline bool SensorRadio::sendLine(const String& json) {
        if (!_started || !_hasPeer) return false;
        if (json.length() > RADIO_MAX_PAYLOAD) {
            Serial.printf("[Sensor] Goi %u byte > %d -> BO.\n",
                        (unsigned)json.length(), RADIO_MAX_PAYLOAD);
            return false;
        }
        return esp_now_send(_peer, (const uint8_t*)json.c_str(), json.length()) == ESP_OK;
    }

// 2. XỬ LÝ DỮ LIỆU ĐẦU RA VÀ ĐẦU VÀO JSON
// Tính năng: Tổng hợp thông số cấu hình ngưỡng an toàn tối đa/tối thiểu của cây trồng hiện tại và gửi xuống mạch cảm biến
static void sensor_push_limits() {
    JsonDocument doc;
    int   c     = active_crop;
    bool valid = (c >= 0 && c < (int)crops.size());
    for (int i = 0; i < P_COUNT; i++) {
        JsonObject o = doc[SENSOR_KEYS[i]].to<JsonObject>();
        if (valid) {
            o["lo"] = crops[c].lo_limit[i];
            o["hi"] = crops[c].hi_limit[i];
        } else {
            o["lo"] = param_meta[i].lo_limit;
            o["hi"] = param_meta[i].hi_limit;
        }
    }
    String out;
    serializeJson(doc, out);
    sensorRadio.sendLine(out);
}

// Tính năng: Giải mã chuỗi JSON nhận được từ cảm biến thật, trích xuất dữ liệu môi trường và cập nhật vào mảng lưu trữ toàn cục
static void sensor_parse_line(const String& line) {
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;

    bool got_any = false;
    for (int i = 0; i < P_COUNT; i++) {
        JsonVariant v = doc[SENSOR_KEYS[i]];
        if (!v.isNull() && (v.is<float>() || v.is<int>())) {
            sensor_value[i] = v.as<float>();
            sensor_valid[i] = true;
            got_any = true;
        } else {
            sensor_valid[i] = false;
        }
    }
    if (got_any) {
        sensor_last_rx_ms = millis();
        sensor_push_limits();
    }
}

// 3. THUẬT TOÁN GIẢ LẬP SỐ LIỆU SENSOR
static bool sim_seeded = false;

// Tính năng: Khởi tạo giá trị gốc ban đầu cho bộ giả lập dựa trên điểm đặt mong muốn (setpoint) của loại cây trồng
static void sim_seed() {
    int   c     = active_crop;
    bool valid = (c >= 0 && c < (int)crops.size());
    for (int i = 0; i < P_COUNT; i++) {
        float sp;
        if (valid) sp = crops[c].setpoint[i];
        else       sp = param_meta[i].default_setpoint;
        sensor_value[i] = clamp_phys(i, sp);
        sensor_valid[i] = true;
    }
    sim_seeded = true;
}

// Tính năng: Thực thi mô phỏng thuật toán toán học Ornstein-Uhlenbeck giúp số liệu cảm biến biến thiên ngẫu nhiên liên tục quanh điểm đặt
static void sim_step() {
    if (!sim_seeded) { sim_seed(); return; }

    int   c     = active_crop;
    bool valid = (c >= 0 && c < (int)crops.size());

    for (int i = 0; i < P_COUNT; i++) {
        float sp_raw;
        if (valid) sp_raw = crops[c].setpoint[i];
        else       sp_raw = param_meta[i].default_setpoint;
        float sp = clamp_phys(i, sp_raw);
        float range = PHYS_MAX[i] - PHYS_MIN[i];

        float v = sensor_value[i];
        v += SIM_PULL * (sp - v);
        v += frand(-1.0f, 1.0f) * SIM_STEP * range;

        sensor_value[i] = clamp_phys(i, v);
        sensor_valid[i] = true;
    }
    sensor_last_rx_ms = millis();
}

// 4. HIỂN THỊ HỘP THOẠI XÁC NHẬN GHÉP CẶP
static lv_obj_t* pair_mbox    = NULL;
static lv_obj_t* pair_cnt_lbl = NULL;

// Tính năng: Giải phóng vùng nhớ và xóa hộp thoại (modal) xác nhận ghép cặp khỏi màn hình giao diện đồ họa
static void pair_close() {
    if (pair_mbox) { lv_obj_del(pair_mbox); pair_mbox = NULL; pair_cnt_lbl = NULL; }
}

// Tính năng: Hàm callback xử lý sự kiện khi người dùng click nút "GHÉP" trên màn hình hiển thị
static void pair_accept_cb(lv_event_t* e) {
    sensorRadio.acceptPair();
    strncpy(sensor_mac, sensorRadio.peerMacStr().c_str(), sizeof(sensor_mac) - 1);
    sensor_mac[sizeof(sensor_mac) - 1] = '\0';
    storage_save_config();
    pair_close();
}

// Tính năng: Hàm callback xử lý sự kiện khi người dùng click nút "TỪ CHỐI" trên màn hình hiển thị
static void pair_reject_cb(lv_event_t* e) {
    sensorRadio.rejectPair();
    pair_close();
}

// Tính năng: Tạo và hiển thị hộp thoại giao diện UI thông báo, yêu cầu người dùng xác nhận kết nối thiết bị cảm biến mới phát hiện
static void pair_show(const uint8_t m[6]) {
    if (pair_mbox) return;

    pair_mbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(pair_mbox, 320, 240);
    lv_obj_center(pair_mbox);
    lv_obj_set_style_bg_color(pair_mbox, COL_BG, 0);
    lv_obj_set_style_bg_opa(pair_mbox, LV_OPA_80, 0);
    lv_obj_set_style_border_width(pair_mbox, 0, 0);
    lv_obj_set_style_pad_all(pair_mbox, 0, 0);
    lv_obj_clear_flag(pair_mbox, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(pair_mbox);
    lv_obj_set_size(card, 284, 176);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, COL_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, "GHEP CAM BIEN?");
    lv_obj_set_style_text_font(t, FONT_TITLE, 0);
    lv_obj_set_style_text_color(t, COL_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    char buf[40];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    lv_obj_t* mac = lv_label_create(card);
    lv_label_set_text(mac, buf);
    lv_obj_set_style_text_font(mac, FONT_VALUE, 0);
    lv_obj_set_style_text_color(mac, COL_ACCENT, 0);
    lv_obj_align(mac, LV_ALIGN_TOP_MID, 0, 26);

    lv_obj_t* hint = lv_label_create(card);
    lv_label_set_text(hint, "Chi bam GHEP neu dung con cam bien\ncua BAN. Ghep nham la nhan so cua\nfarm khac.");
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, COL_TEXT_DIM, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 52);

    pair_cnt_lbl = lv_label_create(card);
    lv_label_set_text(pair_cnt_lbl, "30s");
    lv_obj_set_style_text_font(pair_cnt_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(pair_cnt_lbl, COL_TEXT_DIM, 0);
    lv_obj_align(pair_cnt_lbl, LV_ALIGN_BOTTOM_MID, 0, 2);

    lv_obj_t* bOk = lv_btn_create(card);
    lv_obj_set_size(bOk, 120, 38);
    lv_obj_align(bOk, LV_ALIGN_BOTTOM_LEFT, 4, -18);
    lv_obj_set_style_bg_color(bOk, COL_ON, 0);
    lv_obj_add_event_cb(bOk, pair_accept_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lOk = lv_label_create(bOk);
    lv_label_set_text(lOk, "GHEP");
    lv_obj_center(lOk);

    lv_obj_t* bNo = lv_btn_create(card);
    lv_obj_set_size(bNo, 120, 38);
    lv_obj_align(bNo, LV_ALIGN_BOTTOM_RIGHT, -4, -18);
    lv_obj_set_style_bg_color(bNo, COL_BORDER, 0);
    lv_obj_add_event_cb(bNo, pair_reject_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lNo = lv_label_create(bNo);
    lv_label_set_text(lNo, "TU CHOI");
    lv_obj_center(lNo);
}

// 5. ĐỒNG BỘ THỜI GIAN TIMER TRONG LVGL
// Tính năng: Timer định kỳ kích hoạt hàm gửi tín hiệu quảng bá beacon của vô tuyến ra không gian
static void sensor_beacon_cb(lv_timer_t* t) { sensorRadio.beacon(); }

// Tính năng: Timer quét hàng đợi vô tuyến liên tục để lấy chuỗi dữ liệu thô ra giải mã JSON trên luồng UI chính
static void sensor_radio_poll_cb(lv_timer_t* t) {
    String line;
    while (sensorRadio.popLine(line)) {
        if (sensor_source != SOURCE_RADIO) continue;   // Hủy xử lý gói tin khi đang bật chế độ giả lập
        sensor_parse_line(line);
    }

    // Bắn nốt các gói {"pa"} còn lại (nếu có) của lượt accept gần nhất — không chặn, chạy trên luồng LVGL.
    // Dùng chung timer 20ms này để giữ đúng nhịp ~20ms giữa các lần gửi như bản delay(20) cũ.
    sensorRadio.pumpResendAccept();
}

// Tính năng: Timer giám sát yêu cầu ghép cặp thiết bị và thực hiện cập nhật thời gian đếm ngược lên giao diện hộp thoại
static void sensor_pair_poll_cb(lv_timer_t* t) {
    uint8_t mac[6];
    if (sensorRadio.pendingPair(mac)) {
        if (!pair_mbox) pair_show(mac);
        if (pair_cnt_lbl) {
            char b[8];
            snprintf(b, sizeof(b), "%lus", (unsigned long)((sensorRadio.pairRemainMs() + 999) / 1000));
            lv_label_set_text(pair_cnt_lbl, b);
        }
    } else if (pair_mbox) {
        pair_close();
    }
}

// Tính năng: Timer định kỳ kiểm tra trạng thái Timeout của cảm biến phần cứng, cập nhật luồng dữ liệu giả lập và in log Monitor
inline static void sensor_tick_cb(lv_timer_t* t) {
    if (sensor_source == SOURCE_SIM) {
        sim_step();
    } else if (millis() - sensor_last_rx_ms > SENSOR_TIMEOUT_MS) {
        for (int i = 0; i < P_COUNT; i++) sensor_valid[i] = false;
    }

    String log;
    for (int i = 0; i < P_COUNT; i++) {
        if (i > 0) log += " | ";
        log += SENSOR_KEYS[i];
        log += ": ";
        if (sensor_valid[i]) log += String(sensor_value[i], 2);
        else                 log += String("--");
    }
    Serial.println(log);
}

// Tính năng: Trì hoãn việc khởi động hệ thống vô tuyến (Late Start) nhằm đợi kết nối WiFi của Router ổn định trước để đồng bộ kênh sóng
static void sensor_late_start_cb(lv_timer_t* t) {
    static uint32_t t0 = millis();

    bool gave_up = (millis() - t0 > 20000);
    if (WiFi.status() != WL_CONNECTED && !gave_up) return;

    if (!sensorRadio.begin(1)) return;

    if (sensor_mac[0]) sensorRadio.restorePeer(sensor_mac);   // Khôi phục liên kết cũ tránh ghép lại
    lv_timer_create(sensor_beacon_cb, 1000, NULL);
    lv_timer_del(t);
}

// Tính năng: Gửi lệnh đóng cắt rơ-le điều khiển máy bơm nước xuống trực tiếp bo mạch cảm biến phần cứng ngoại vi
static void sensor_pump_push_cb(lv_timer_t* t) {
    if (!sensorRadio.hasPeer()) return;
    JsonDocument doc;
    doc["pump"] = (int)pump_on;
    String out;
    serializeJson(doc, out);
    sensorRadio.sendLine(out);
}

// Tính năng: Thay đổi chế độ nguồn dữ liệu đầu vào hệ thống giữa phần cứng thực tế (ESP-NOW) và trình giả lập toán học (Simulation)
static void sensor_set_source(uint8_t mode) {
    if (mode != SOURCE_SIM) mode = SOURCE_RADIO;
    if (mode == sensor_source) return;
    sensor_source = mode;

    if (mode == SOURCE_SIM) {
        sim_seed();
        sensor_last_rx_ms = millis();
        Serial.println("[Sensor] -> MO PHONG (tu sinh so)");
    } else {
        sim_seeded = false;
        for (int i = 0; i < P_COUNT; i++) sensor_valid[i] = false;
        sensor_last_rx_ms = 0;
        Serial.println("[Sensor] -> ESP-NOW (doc cam bien that)");
    }

    storage_save_config();
    cloud_push_now = true;
}

// Tính năng: Khởi động toàn bộ các bộ định thời (LVGL Timers) phục vụ cho việc quản lý, đồng bộ và giao tiếp dữ liệu cảm biến
static void sensor_start_timers() {
    lv_timer_create(sensor_late_start_cb, 500,  NULL);
    lv_timer_create(sensor_radio_poll_cb, 20,   NULL);
    lv_timer_create(sensor_pair_poll_cb,  500,  NULL);
    lv_timer_create(sensor_pump_push_cb,  500,  NULL);
    lv_timer_create(sensor_tick_cb,       1000, NULL);
}