//  Phân chia nhân cho ESP32-CYD
//     core 1 (LVGL) : Giao diện của ESP
//     core 0 (task) : MQTT, Telegram, Firebase, giao tiếp ESP-NOW


#include "AppData.h"        //  Lưu dữ liệu
#include "AppSensor.h"      //  Giao tiếp với ESP32-Sensor qua ESP-NOW
#include "AppWiFi.h"        //  Giao thức WiFi
#include "AppUI.h"          //  Giao diện hiển thị
#include "AppMqtt.h"        //  Giao tiếp Web <-> ESP32-CYD
#include "AppTelegram.h"    //  Cảnh báo qua Telegram
#include "AppFirebase.h"    //  Lưu dữ liệu lên Firebase


void setup() {
    Serial.begin(115200);                           //  Bật Serial giao tiếp với máy tính
    screen.begin(true, 3, false);                   //  Khởi tạo màn hình nằm ngang

    storage_init();                                 //  Kiểm tra thẻ nhớ (không có thì lưu tạm vào NVS)
    storage_load_config();                          //  Đọc cấu hình: cây trồng, các ngưỡng, WiFi, Chat ID

    wifi_load_forced_credentials();                 //  Load WiFi ép cứng trong code (chỉ để test)
    wifi_connect_blocking(WIFI_BOOT_TIMEOUT_MS);    //  Kết nối WiFi, không được thì chạy offline

    ui_init();                                      //  Vẽ giao diện màn hình

    sensor_start_timers();                          //  Bật ESP-NOW, phát Beacon để ESP32-Sensor tìm được kênh
    mqtt_start();                                   //  Đăng ký MQTT, đồng bộ 2 chiều với web
    telegram_start();                               //  Mở Telegram để đẩy cảnh báo
    firebase_start();                               //  Mở Firebase để lưu lịch sử

    lv_timer_create(storage_csv_export_cb,    15000, NULL);   //  Xuất CSV ra thẻ nhớ mỗi 15s
    lv_timer_create(storage_hotplug_check_cb, 20000, NULL);   //  Dò cắm/rút thẻ nhớ giữa chừng mỗi 20s
}


void loop() {
    screen.update();                //  Vẽ giao diện LVGL liên tục
    delay(2);

    if (cloud_push_now) {           //  Đồng bộ giao diện ESP32-CYD với Web ngay khi có nút được nhấn
        cloud_push_now = false;
        cloud_publish_full_cb(NULL);
    }
}
