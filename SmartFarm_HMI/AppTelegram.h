#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <vector>
#include "AppData.h"
#include "AppMqtt.h"

//  Token của bot Telegram. Còn Chat ID thì KHÔNG để ở đây — người dùng nhập trên màn hình
//  (Cài đặt > Telegram ID), lưu vào config, đổi lúc nào cũng được mà không phải nạp lại firmware.
#define TELEGRAM_BOT_TOKEN "8855083157:AAHt1HYyPz7m-MAN_0-W9a65ThYEkraqcOc"

#define ALERT_CHECK_MS     2000     //  Cứ 2 giây kiểm tra ngưỡng một lần
#define ALERT_HOLD_MS      30000    //  Phải vượt ngưỡng liên tục 30 giây mới báo lần đầu
#define TELEGRAM_QUEUE_CAP 12       //  Sức chứa hàng đợi tin, chặn tràn RAM nếu mạng chết lâu

//  Phần 1. GỬI GÓI TIN QUA TELEGRAM (Core 0)
    class TelegramAPI {
    public:
        void begin(const char* token, const char* chatId);
        void setChat(const char* chatId);
        void send(const String& text);
        bool enabled() const { return _on; }

        void loopTask();

    private:
        String _token, _chatId;
        bool   _on = false;

        std::vector<String> _queue;
        SemaphoreHandle_t   _mutex = nullptr;
        TaskHandle_t        _task  = nullptr;

        void _ensureTask();
        bool _post(const String& text);
    };

    static TelegramAPI telegram;

    //  Cầu nối giúp FreeRTOS chạy được hàm C++ loopTask() dưới nền
    inline void telegram_task_runner(void* pv) {
        static_cast<TelegramAPI*>(pv)->loopTask();
        vTaskDelete(nullptr);
    }

    //  Hàm _ensureTask: Dựng task gửi tin. Có token là dựng luôn, kể cả chưa có Chat ID —
    //  người dùng nhập Chat ID trên màn hình lúc nào cũng được, khỏi phải reboot.
    inline void TelegramAPI::_ensureTask() {
        if (!_mutex) _mutex = xSemaphoreCreateMutex();
        if (!_task && _token.length() > 0)
            xTaskCreatePinnedToCore(telegram_task_runner, "Telegram", 12288, this, 1, &_task, 0);
    }

    //  Hàm begin: Nạp token + Chat ID lúc khởi động, rồi dựng task
    inline void TelegramAPI::begin(const char* token, const char* chatId) {
        //  Chép token vào (nếu con trỏ rỗng thì để chuỗi trống)
        if (token) _token = token;
        else       _token = "";
        //  Chép Chat ID vào tương tự
        if (chatId) _chatId = chatId;
        else        _chatId = "";

        _on = _token.length() > 0 && _chatId.length() > 0;   //  Bật cảnh báo khi có đủ cả hai
        _ensureTask();

        //  Báo trạng thái ra Serial
        if (_on) Serial.printf("[Telegram] BAT — chat_id=%s\n", _chatId.c_str());
        else     Serial.println("[Telegram] Chua co Chat ID -> vao Cai dat > Telegram ID.");
    }

    //  Hàm setChat: Đổi người nhận cảnh báo (khi người dùng nhập/đổi Chat ID trên màn hình)
    inline void TelegramAPI::setChat(const char* chatId) {
        if (chatId) _chatId = chatId;
        else        _chatId = "";

        _on = _token.length() > 0 && _chatId.length() > 0;
        _ensureTask();

        //  Đổi người nhận rồi thì tin đang chờ trong hàng đợi là của người CŨ -> vứt hết đi
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _queue.clear();
            xSemaphoreGive(_mutex);
        }
    }

    //  Hàm send: Xếp một tin nhắn vào hàng đợi chờ gửi
    inline void TelegramAPI::send(const String& text) {
        //  Chưa bật, tin rỗng, hoặc chưa có khóa bảo vệ thì thôi
        if (!_on || text.length() == 0 || !_mutex) return;
        //  Mượn khóa, nếu bận thì bỏ qua lượt này
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
        //  Còn chỗ trong hàng đợi thì thêm vào
        if (_queue.size() < TELEGRAM_QUEUE_CAP) _queue.push_back(text);
        xSemaphoreGive(_mutex);
    }

    //  Hàm loopTask: Vòng lặp ngầm liên tục lấy tin ra khỏi hàng đợi và gửi đi
    inline void TelegramAPI::loopTask() {
        for (;;) {
            String job;
            bool   have = false;

            //  Mở hàng đợi, lấy tin đầu tiên ra (nếu có)
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (!_queue.empty()) {
                    job = _queue.front();
                    _queue.erase(_queue.begin());
                    have = true;
                }
                xSemaphoreGive(_mutex);
            }

            if (have) {
                //  Chỉ gửi khi đang có WiFi. Tính kết quả gửi thành công hay không
                bool sent = false;
                if (WiFi.status() == WL_CONNECTED) sent = _post(job);

                if (!sent) {
                    //  Mất mạng hoặc gửi hỏng -> trả tin lại ĐẦU hàng đợi để thử lại vòng sau
                    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        if (_queue.size() < TELEGRAM_QUEUE_CAP) _queue.insert(_queue.begin(), job);
                        xSemaphoreGive(_mutex);
                    }
                    vTaskDelay(pdMS_TO_TICKS(3000));    //  Nghỉ 3 giây rồi thử lại, khỏi dồn dập
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    //  Hàm _post: Gửi thật một tin nhắn lên máy chủ Telegram bằng HTTPS
    inline bool TelegramAPI::_post(const String& text) {
        //  setInsecure(): vẫn mã hóa TLS, chỉ là không kiểm tra chứng chỉ máy chủ. Nhét CA bundle vào
        //  ESP32 tốn flash và phải cập nhật mỗi khi Telegram đổi chứng chỉ. Cảnh báo pH thì thế này là đủ;
        //  đừng bê cách này sang thứ gì dính tới tiền bạc.
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(8);

        HTTPClient http;
        String url = "https://api.telegram.org/bot" + _token + "/sendMessage";
        if (!http.begin(client, url)) return false;

        http.addHeader("Content-Type", "application/json");
        http.setTimeout(8000);

        //  Gửi bằng JSON body -> khỏi phải mã hóa URL bằng tay, chỉ cần escape đúng chuẩn JSON
        String body = "{\"chat_id\":\"" + _chatId + "\",\"text\":\"";
        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            switch (c) {
                case '\"': body += "\\\""; break;
                case '\\': body += "\\\\"; break;
                case '\n': body += "\\n";  break;
                case '\r': break;
                default:   body += c;
            }
        }
        body += "\"}";

        int code = http.POST(body);
        http.end();

        //  Mã 200 là gửi thành công
        if (code == 200) return true;
        Serial.printf("[Telegram] LOI HTTP %d — kiem tra TOKEN/CHAT_ID\n", code);
        return false;
    }

//  PHẦN 2. XỬ LÍ BÁO ĐỘNG LVGL
    //  Các mốc thời gian nhắc lại: lần 1 sau 1 phút, rồi 5 phút, 15 phút, 30 phút
    static const uint32_t ALERT_REPEAT_MS[]    = { 60000UL, 300000UL, 900000UL, 1800000UL };
    static const int      ALERT_REPEAT_COUNT   = sizeof(ALERT_REPEAT_MS) / sizeof(ALERT_REPEAT_MS[0]);
    static const uint32_t ALERT_REPEAT_CAP_MS  = 86400000UL;   //  Trần tối đa: 24 giờ mỗi lần nhắc

    //  Trạng thái báo động riêng cho từng thông số
    static bool     alert_sent[P_COUNT]  = {false};   //  Đã báo lần đầu chưa
    static uint32_t alert_since[P_COUNT] = {0};       //  Bắt đầu vượt ngưỡng từ lúc nào
    static uint32_t alert_last[P_COUNT]  = {0};       //  Lần gửi gần nhất là lúc nào
    static uint8_t  alert_step[P_COUNT]  = {0};       //  Đang ở bậc nhắc lại thứ mấy

    //  Hàm alert_repeat_interval: Tính khoảng cách tới lần nhắc kế tiếp, dựa theo bậc hiện tại
    static uint32_t alert_repeat_interval(uint8_t step) {
        //  Còn trong bảng mốc thì lấy thẳng
        if (step < ALERT_REPEAT_COUNT) return ALERT_REPEAT_MS[step];

        //  Vượt bảng thì gấp đôi mốc cuối cùng theo số bậc dôi ra
        uint32_t doublings = step - (ALERT_REPEAT_COUNT - 1);
        if (doublings > 40) doublings = 40;           //  Chặn lại kẻo dịch bit tràn số
        uint64_t interval = (uint64_t)ALERT_REPEAT_MS[ALERT_REPEAT_COUNT - 1] << doublings;

        //  Không cho vượt quá trần 24h
        if (interval > ALERT_REPEAT_CAP_MS) return ALERT_REPEAT_CAP_MS;
        return (uint32_t)interval;
    }

    //  Hàm format_duration: Đổi số giây thành chuỗi dễ đọc (90 -> "1p", 7200 -> "2h").
    //  Lấy đơn vị lớn nhất vừa đủ; in ra "86400s" thì không ai đọc nổi.
    static String format_duration(uint32_t seconds) {
        if (seconds < 60) return String(seconds) + "s";
        uint32_t minutes = seconds / 60;
        if (minutes < 60) return String(minutes) + "p";
        uint32_t hours = minutes / 60;
        if (hours < 24)   return String(hours) + "h";
        return String(hours / 24) + "d";
    }

    //  Hàm alert_check_cb: Chạy định kỳ, kiểm tra từng thông số và quyết định gửi cảnh báo
    static void alert_check_cb(lv_timer_t* t) {
        //  Chưa bật Telegram hoặc chưa có WiFi thì thôi
        if (!telegram.enabled() || !mqttCloud.isWiFiConnected()) return;

        //  Chỉ xét cây đang được chọn
        int crop = active_crop;
        if (crop < 0 || crop >= (int)crops.size()) return;

        uint32_t now = millis();

        for (int i = 0; i < P_COUNT; i++) {
            float value;
            //  Mất số liệu thì đứng im chờ, KHÔNG kết luận vội
            if (!param_eff(crop, i, &value)) continue;

            float lo  = crops[crop].lo_limit[i];
            float hi  = crops[crop].hi_limit[i];
            bool  bad = (value < lo || value > hi);      //  Có đang vượt ngưỡng không

            if (bad) {
                //  Ghi lại thời điểm bắt đầu vượt (nếu chưa ghi)
                if (alert_since[i] == 0) alert_since[i] = now;
                //  Chưa vượt đủ lâu -> im lặng, chờ tiếp
                if (now - alert_since[i] < ALERT_HOLD_MS) continue;

                //  Tới lúc gửi chưa: hoặc chưa báo lần nào, hoặc đã đủ khoảng cách nhắc lại
                bool due = false;
                if (!alert_sent[i]) due = true;
                else if (now - alert_last[i] >= alert_repeat_interval(alert_step[i])) due = true;
                if (!due) continue;

                //  Soạn tin cảnh báo
                char text[230];
                String held = format_duration((now - alert_since[i]) / 1000);
                snprintf(text, sizeof(text),
                        "\xE2\x9A\xA0\xEF\xB8\x8F CANH BAO — %s\n"
                        "%s: %.2f %s\n"
                        "Nguong cho phep: %.2f – %.2f\n"
                        "Da vuot lien tuc %s.\n"
                        "Cay: %s | IP: %s",
                        device_name,
                        param_meta[i].full_name, value, param_meta[i].unit,
                        lo, hi, held.c_str(),
                        crops[crop].name,
                        WiFi.localIP().toString().c_str());
                telegram.send(String(text));

                //  Lần đầu thì đặt cờ + về bậc 0; các lần sau thì tăng bậc lên (để nhắc thưa dần)
                if (!alert_sent[i]) {
                    alert_sent[i] = true;
                    alert_step[i] = 0;
                } else if (alert_step[i] < 250) {
                    alert_step[i]++;
                }
                alert_last[i] = now;

            } else {
                //  Đã về trong ngưỡng. Nếu trước đó từng báo động thì gửi tin "đã ổn"
                if (alert_sent[i]) {
                    char text[200];
                    snprintf(text, sizeof(text),
                            "\xE2\x9C\x85 DA ON — %s\n%s: %.2f %s (trong nguong %.2f – %.2f)",
                            device_name,
                            param_meta[i].full_name, value, param_meta[i].unit, lo, hi);
                    telegram.send(String(text));
                    alert_sent[i] = false;
                }
                //  Xóa đồng hồ đếm để lần vượt sau tính lại từ đầu
                alert_since[i] = 0;
                alert_step[i]  = 0;
            }
        }
    }

    //  Hàm alert_apply_chat_cb: Bắt tín hiệu người dùng vừa nhập Chat ID mới trên màn Cài đặt.
    //  (Màn hình chỉ dựng cờ báo — nó không biết gì về lớp TelegramAPI, và cũng không nên biết.)
    static void alert_apply_chat_cb(lv_timer_t* t) {
        if (!telegram_chat_changed) return;
        telegram_chat_changed = false;

        telegram.setChat(telegram_chat_id);

        //  Có Chat ID mới thì gửi tin chào để người dùng biết đã kết nối; ngược lại là vừa xóa Chat ID
        if (strlen(telegram_chat_id) > 0) {
            telegram.send(String("Da ket noi ") + device_name + " — canh bao Telegram BAT.");
            Serial.printf("[Telegram] Chat ID moi: %s\n", telegram_chat_id);
        } else {
            Serial.println("[Telegram] Da xoa Chat ID -> TAT canh bao.");
        }
    }

    //  Hàm telegram_start: Khởi động hệ thống cảnh báo Telegram
    static void telegram_start() {
        telegram.begin(TELEGRAM_BOT_TOKEN, telegram_chat_id);
        lv_timer_create(alert_check_cb,      ALERT_CHECK_MS, NULL);   //  Định kỳ kiểm tra ngưỡng
        lv_timer_create(alert_apply_chat_cb, 500,            NULL);   //  Định kỳ bắt tín hiệu đổi Chat ID
    }
