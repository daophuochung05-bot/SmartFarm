#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include "AppData.h"
#include "AppMqtt.h"

#define FIREBASE_HOST     "smartfarm-95908-default-rtdb.asia-southeast1.firebasedatabase.app"   //  URL DATABASE
#define FIREBASE_SECRET   ""                                                                    //  Hiện tại ai cũng có thể gửi dữ liệu lên mà chưa cần mật khẩu
#define HISTORY_PUSH_MS   60000                                                                 //  Lưu dữ liệu mỗi phút 1 lần


//  Phần 1: Các hàm chạy bên dưới(Core 0)
    class HistoryDB {
    public:
        void begin(const char* host, const char* roomId, const char* mac, const char* secret = "");
        void offer(const String& rowJson);      // luồng LVGL nạp 1 dòng vào hộp gửi

        void loopTask();                        // thân task, đừng gọi tay

    private:
        String _url, _pending;
        bool   _dirty = false;

        SemaphoreHandle_t _mutex = nullptr;
        TaskHandle_t      _task  = nullptr;

        bool _post(const String& body);
    };

    static HistoryDB historyDB;

    //  Cầu nối (C-wrapper) giúp FreeRTOS chạy hàm C++ (loopTask) dưới nền
    inline void history_task_runner(void* pv) {
        static_cast<HistoryDB*>(pv)->loopTask();
        vTaskDelete(nullptr);
    }

    //  Hàm Begin: Khởi tạo kết nối mạng và tạo luồng gửi Firebase chạy ngầm ở Core 0
    inline void HistoryDB::begin(const char* host, const char* roomId, const char* mac, const char* secret) {
        //  Kiểm tra có địa chỉ Firebase chưa, chưa có thì khỏi ghi
        if (!host || !host[0]) {
            Serial.println("[Firebase] Chua dat host -> KHONG ghi lich su.");
            return;
        }

        //  Kiểm tra có ID Telegram chưa, nếu có thì ghi vào, không có thì ghi vào phòng unset
        String room;
        if (roomId && roomId[0]) {
            // Nếu có Telegram ID hợp lệ -> Lấy ID đó làm tên phòng
            room = String(roomId);
        } else {
            // Nếu ID bị trống hoặc lỗi -> Mặc định đưa vào phòng "unset"
            room = "unset";
            Serial.println("[Firebase] CANH BAO: chua co Telegram ID -> lich su nam trong phong chung \"unset\".");
        }

        // Chuyển đổi an toàn địa chỉ MAC từ con trỏ sang chuỗi String
        String macStr = "";
        if (mac) {
            macStr = String(mac); 
        }

        // Lắp ráp đường link Firebase hoàn chỉnh dẫn đến đúng thư mục của thiết bị
        _url = "https://" + String(host) + "/farm/" + room + "/" + macStr + "/h.json";

        // Nếu có mật khẩu bảo mật, tự động nối thêm tham số xác thực vào cuối link
        if (secret && secret[0]) {
            _url += "?auth=" + String(secret);
        }

        // Tạo khóa bảo vệ (Mutex) để tránh xung đột bộ nhớ giữa luồng mạng và màn hình
        if (!_mutex) {
            _mutex = xSemaphoreCreateMutex();
        }

        // Tạo luồng gửi dữ liệu chạy ngầm ở Core 0, dành riêng 12KB RAM để kết nối mạng
        if (!_task) {
            xTaskCreatePinnedToCore(history_task_runner, "HistoryDB", 12288, this, 1, &_task, 0);
        }

        // In đường link hoàn chỉnh lên cổng Serial để lập trình viên tiện kiểm tra lỗi
        Serial.printf("[Firebase] %s\n", _url.c_str());
    }

    // Hàm offer: Thêm dữ liệu lịch sử mới vào hộp thư chờ gửi
    inline void HistoryDB::offer(const String& rowJson) {
        // Chốt chặn: Thoát ngay nếu chưa tạo khóa bảo vệ hoặc chuỗi dữ liệu bị rỗng
        if (!_mutex || rowJson.length() == 0) return;
        
        // Thử mượn chìa khóa trong tối đa 20ms, nếu thất bại (bận) thì bỏ qua lượt này
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
        
        _pending = rowJson;      // Ghi đè dữ liệu mới nhất (mới nhất thắng, tránh đầy bộ nhớ RAM)
        _dirty   = true;         // Dựng cờ báo hiệu đang có dữ liệu mới chờ gửi đi
        
        xSemaphoreGive(_mutex);  // Trả lại chìa khóa ngay lập tức để giải phóng bộ nhớ
    }

    //  Hàm Looptask: Vòng lặp chạy ngầm liên tục kiểm tra và gửi dữ liệu lên Firebase
    inline void HistoryDB::loopTask() {
        for (;;) {
            String job;
            bool   have = false;

            // Thử mượn khóa bảo vệ trong tối đa 50ms để mở hộp thư
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (_dirty) { 
                    job = _pending;    // Copy dữ liệu sang biến tạm để xử lý riêng
                    _dirty = false;    // Xóa cờ báo thư mới (hộp thư đã trống)
                    have = true;       // Đánh dấu: "Đã lấy được thư thành công"
                }
                xSemaphoreGive(_mutex);
            }

            // Nếu lấy được thư và thiết bị đã kết nối WiFi thành công rồi nghỉ 200ms để ESP32 không quá tải
            if (have && WiFi.status() == WL_CONNECTED) _post(job);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    //  Hàm _post: gửi dữ liệu JSON thực tế lên Firebase bằng HTTPS POST
    inline bool HistoryDB::_post(const String& body) {
        WiFiClientSecure client;
        client.setInsecure();       // Cho phép kết nối mã hóa HTTPS mà không cần kiểm tra chứng chỉ SSL (chạy nhanh hơn)
        client.setTimeout(8);       // Đặt thời gian chờ kết nối mạng tối đa là 8 giây

        HTTPClient http;
        // Bắt đầu khởi tạo phiên kết nối HTTP với địa chỉ URL đã cấu hình
        if (!http.begin(client, _url)) {
            Serial.println("[Firebase] http.begin that bai");
            return false;           // Kết nối thất bại -> Thoát và báo lỗi
        }
        
        http.addHeader("Content-Type", "application/json"); // Khai báo gửi dữ liệu dưới dạng chuỗi JSON
        http.setTimeout(8000);      // Đặt thời gian chờ phản hồi từ máy chủ tối đa 8 giây

        int code = http.POST(body);     // Gửi yêu cầu POST -> Firebase tự sinh ID ngẫu nhiên, không bị ghi đè dữ liệu cũ
        http.end();                     // Đóng kết nối để giải phóng bộ nhớ RAM

        // Kiểm tra phản hồi từ Firebase (mã 200 hoặc 201 là thành công)
        if (code == 200 || code == 201) {
            Serial.printf("[Firebase] OK  %s\n", body.c_str());
            return true;            // Gửi thành công
        }
        
        // In mã lỗi HTTP cụ thể nếu thất bại (ví dụ: 404 không tìm thấy, 403 sai quyền truy cập)
        Serial.printf("[Firebase] LOI HTTP %d — kiem tra host/rules\n", code);
        return false;               // Gửi thất bại
    }



//  Phần 2: Các hàm chạy bên trên LVGL(Core 1)
    // Hàm callback chạy theo chu kỳ của thư viện màn hình (LVGL) để đóng gói dữ liệu
    static void history_push_cb(lv_timer_t* t) {
        // Chốt chặn an toàn: Thoát ngay nếu chưa cấu hình Server hoặc chưa có WiFi
        if (strlen(FIREBASE_HOST) == 0)   return;
        if (!mqttCloud.isWiFiConnected()) return;

        time_t now = time(nullptr);
        if (now < 1600000000) return;       // Thoát nếu thời gian thực (NTP) chưa đồng bộ, tránh ghi sai mốc giờ

        int crop = active_crop;
        if (crop < 0 || crop >= (int)crops.size()) return; // Thoát nếu mã cây trồng hiện tại không hợp lệ

        JsonDocument doc;
        doc["t"] = (uint32_t)now;           // Đóng gói mốc thời gian hiện tại vào gói JSON

        bool any = false;
        // Vòng lặp quét qua tất cả các loại cảm biến của hệ thống
        for (int i = 0; i < P_COUNT; i++) {
            float value;
            // Nếu đọc được giá trị hợp lệ từ cảm biến (sau khi đã tính toán bù trừ hiệu chuẩn)
            if (param_eff(crop, i, &value)) { 
                doc[SENSOR_KEYS[i]] = value; // Nhét giá trị cảm biến kèm tên khóa tương ứng vào JSON
                any = true;                  // Xác nhận: "Có ít nhất 1 cảm biến có số liệu"
            }
        }
        if (!any) return;                   // Thoát nếu tất cả cảm biến đều lỗi, tránh gửi gói tin trống rỗng lên mạng

        String row;
        serializeJson(doc, row);            // Nén gói JSON thành một chuỗi văn bản (String)
        historyDB.offer(row);               // Ném chuỗi văn bản vào hộp thư chờ gửi ngầm lên Firebase
    }

    // Hàm khởi động toàn bộ hệ thống gửi lịch sử Firebase
    static void firebase_start() {
        if (strlen(FIREBASE_HOST) == 0) return; // Thoát nếu không có cấu hình máy chủ Firebase
        
        // Khởi tạo luồng mạng chạy ngầm và cấu hình địa chỉ lưu trữ dữ liệu
        historyDB.begin(FIREBASE_HOST, mqtt_room_id, mqttCloud.mac(), FIREBASE_SECRET);
        
        // Kích hoạt bộ định thời LVGL: Tự động gọi hàm history_push_cb sau mỗi khoảng chu kỳ (HISTORY_PUSH_MS)
        lv_timer_create(history_push_cb, HISTORY_PUSH_MS, NULL);
    }
