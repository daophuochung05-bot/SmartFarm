#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "AppData.h"

//  Để trống thì người dùng tự nhập trên màn Cài đặt
//  Điền cứng vào đây dùng lúc test.
#define FORCED_WIFI_SSID      ""
#define FORCED_WIFI_PASS      ""
#define WIFI_BOOT_TIMEOUT_MS  15000     //  Thời gian chờ nối WiFi lúc boot (15 giây)

//  Hàm wifi_up: Trả về true nếu đang kết nối WiFi thành công
static inline bool wifi_up() {
    return WiFi.status() == WL_CONNECTED;
}

//  Hàm wifi_connect_blocking: Nối WiFi lúc boot, chặn luồng cho tới khi xong hoặc hết giờ
static void wifi_connect_blocking(uint32_t timeout_ms) {
    //  Chưa có tên WiFi (SSID) thì thoát ngay, khỏi nối
    if (strlen(wifi_ssid) == 0) return;

    Serial.printf("[WiFi] Noi: %s ", wifi_ssid);
    WiFi.mode(WIFI_STA);                    //  Đặt ESP32 làm máy con (Station), nối vào router
    WiFi.begin(wifi_ssid, wifi_pass);

    //  Chờ tới khi nối được hoặc hết thời gian cho phép
    uint32_t t0 = millis();
    while (!wifi_up() && millis() - t0 < timeout_ms) delay(250);

    //  Báo kết quả ra Serial để tiện theo dõi
    if (wifi_up()) {
        Serial.printf("OK — IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("THAT BAI -> chay tiep o che do offline.");
    }
}

//  Hàm wifi_try_connect: Thử nối SSID/mật khẩu người dùng vừa nhập. Trả về true nếu vào được mạng
static bool wifi_try_connect(uint32_t timeout_ms) {
    //  Chưa nhập tên WiFi thì coi như thất bại
    if (strlen(wifi_ssid) == 0) return false;

    //  Ngắt kết nối cũ hẳn rồi mới nối lại, tránh dính trạng thái cũ
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);

    //  Chờ trong khoảng thời gian cho phép, nối được thì trả về true ngay
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        if (wifi_up()) return true;
        delay(200);
    }

    //  Hết giờ vẫn chưa nối được -> ngắt và trả về false
    WiFi.disconnect(true);
    return false;
}

//  Hàm wifi_load_forced_credentials: Nạp WiFi ép cứng (nếu có điền ở đầu file) lúc boot
static void wifi_load_forced_credentials() {
    //  Không điền SSID ép cứng thì thoát, để người dùng tự nhập trên màn hình (an toàn hơn)
    if (strlen(FORCED_WIFI_SSID) == 0) return;

    //  Chép SSID và mật khẩu ép cứng vào biến toàn cục, cắt bớt nếu quá dài để không tràn bộ nhớ
    strncpy(wifi_ssid, FORCED_WIFI_SSID, sizeof(wifi_ssid) - 1);
    strncpy(wifi_pass, FORCED_WIFI_PASS, sizeof(wifi_pass) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    wifi_pass[sizeof(wifi_pass) - 1] = '\0';
}