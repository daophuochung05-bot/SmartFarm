#pragma once
//  Data — dữ liệu chung của cả máy, và cách nhớ nó xuống thẻ.
//     Phần 1: cây trồng, thông số, số đo, bơm, danh tính, khoá chống ghi đè
//     Phần 2: danh bạ hàm — khai báo trước mọi hàm gọi chéo giữa các file
//     Phần 3: lưu / nạp — có thẻ SD thì ghi file, không thì ghi NVS
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "AppScreen.h"
#include "AppRoomId.h"

//  PHẦN 1 — DỮ LIỆU CHUNG
    //  1.1 Khởi tạo các thông số
        #define FIRMWARE_VERSION "2.0"

        //  Khởi tạo và gán giá trị cho 6 thông số
        enum ParamIndex { P_PH = 0, P_TDS, P_LIGHT, P_AIR_HUM, P_AIR_TEMP, P_WATER_TEMP, P_COUNT };
        //  Khởi tạo keys để gửi Json cho các thông số
        static const char* SENSOR_KEYS[P_COUNT] = { "ph", "tds", "lux", "hum", "air", "h2o" };

        //  Khởi tạo các giá trị
        typedef struct {
            const char* short_name;
            const char* full_name;
            const char* unit;
            float       min_v, max_v;
            float       default_setpoint;
            float       lo_limit, hi_limit;
            lv_color_t  color;
        } ParamMeta;

        static ParamMeta param_meta[P_COUNT] = {
            { "pH",    "pH Level",        "",    0.0f, 14.0f,    6.0f,    5.5f,    6.5f,   lv_color_hex(0x30D158) },
            { "TDS",   "Total Dissolved", "ppm", 0.0f, 1000.0f,  800.0f,  560.0f,  840.0f, lv_color_hex(0xBF5AF2) },
            { "LUX",   "Light Intensity", "lux", 0.0f, 65535.0f, 5000.0f, 3000.0f, 6000.0f,lv_color_hex(0xFFD60A) },
            { "HUM",   "Air Humidity",    "%",   0.0f, 100.0f,   65.0f,   50.0f,   70.0f,  lv_color_hex(0x64D2FF) },
            { "T.AIR", "Air Temp",        "°C",  0.0f, 100.0f,   27.0f,   18.0f,   26.0f,  lv_color_hex(0xFF9F0A) },
            { "T.H2O", "Water Temp",      "°C",  0.0f, 100.0f,   22.0f,   18.0f,   24.0f,  lv_color_hex(0x5E5CE6) },
        };
        //  Ngưỡng vật lí
        static const float PHYS_MIN[P_COUNT] = {  0.0f,    0.0f,     0.0f,   0.0f,   0.0f,   0.0f };
        static const float PHYS_MAX[P_COUNT] = { 14.0f, 1000.0f, 65535.0f, 100.0f, 100.0f, 100.0f };
        //  Thông số rau xà lách(test)
        static const float LETTUCE_SETPOINT[P_COUNT] = { 6.0f, 600.0f, 4000.0f, 70.0f, 22.0f, 20.0f };

    //  1.2 Dữ liệu cây trồng
        //  Khai báo Logo cho cây xà lách
        LV_IMG_DECLARE(Lettuce_logo);
        static const void* CROP_LOGOS[]   = { &Lettuce_logo, NULL };
        static const int   NUM_CROP_LOGOS = 2;

        typedef struct {
            char          name[24];                 //  Tên cây trồng
            const void*   logo;                     //  Địa chỉ con trỏ trỏ tới logo
            int           logo_idx;                 //  Chỉ số con trỏ
            float         value[P_COUNT];           //  Các thông số đo được gần nhất
            float         setpoint[P_COUNT];        //  Các Setpoint
            float         lo_limit[P_COUNT];        //  Các giới hạn dưới
            float         hi_limit[P_COUNT];        //  Các giới hạn trên
            bool          manual[P_COUNT];          //  Các nút bật/tắt chế độ Manual
            bool          manual_val_set[P_COUNT];  //  Xác nhận sau khi bật Manual thì có bật tắt switch nào chưa, nếu chưa đặt tất cả switch ở off
            bool          sw1[P_COUNT];             //  Các nút (+) trong điều khiển thủ công
            bool          sw2[P_COUNT];             //  Các nút (-) trong điều khiển thủ công
        } Crop;

        static std::vector<Crop> crops;             //  Khai báo kiểu vector để dễ dàng thêm các cây trồng khác
        static int  active_crop  = 0;               //  Số thứ tự cây được chọn (chỉ đọc cảm biến, điều khiển và phát cảnh báo của cây này)
        static int  current_page = 0;               //  Số thứ tự của trang đang hiển thị hiện tại

    //  1.3 Số đo hiện tại
        #define SOURCE_RADIO  0                             //  Chế độ đọc tín hiệu của ESP-Sensor qua ESP-NOW
        #define SOURCE_SIM    1                             //  Chế độ mô phòng tín hiệu

        static uint8_t  sensor_source = SOURCE_RADIO;       //  Chọn chế độ đọc tín hiệu của ESP-Sensor qua ESP-NOW
        static float    sensor_value[P_COUNT] = {0};        //  Biến lưu giá trị ESP32-Sensor đọc về
        static bool     sensor_valid[P_COUNT] = {false};    //  Biến xác nhận có kết nối được với ESP32-Sensor chưa
        static uint32_t sensor_last_rx_ms     = 0;          //  Biến đếm thời gian kết nối lần trước của ESP32-Sensor để phát hiện mất kết nối
        static char     sensor_mac[18]        = "";         // MAC con cảm biến đã ghép, nhớ qua reboot

    //  1.4 Các cơ cấu chấp hành
        static bool     pump_on      = false;
        static uint32_t pump_lock_ms = 0;

    //  1.5 Cấu hình mạng
        #define DEVICE_NAME_MAXLEN  40                                              //  Độ dài tối đa của tên thiết bị
        #define ROOM_ID_MAXLEN      48                                              //  Độ dài tối đa của ID phòng 
        #define WIFI_SSID_MAXLEN    33                                              //  Độ dài tối đa của tên WiFi
        #define WIFI_PASS_MAXLEN    65                                              //  Độ dài tối đa của password WiFi
        #define TELEGRAM_ID_MAXLEN  24                                              //  Độ dài tối đa của ID Telegram

        static char device_name[DEVICE_NAME_MAXLEN] = "LayerMaker's Farm";          //  Tên thiết bị mặc định
        static char mqtt_room_id[ROOM_ID_MAXLEN]    = "unset";                      //  ID phòng
        static char wifi_ssid[WIFI_SSID_MAXLEN]     = "";                           //  Tên WiFi
        static char wifi_pass[WIFI_PASS_MAXLEN]     = "";                           //  Password của WiFi
        static char telegram_chat_id[TELEGRAM_ID_MAXLEN] = "";                      //  ID Telegram

        static bool mqtt_online           = false;                                  //  Cờ báo kết nối MQTT
        static bool telegram_chat_changed = false;                                  //  Cờ kết nối Telegram
        static bool param_alarm[P_COUNT] = {false};                                 //  Cờ cảnh báo 

    //  1.6 Chống ghi đè MQTT
        #define LOCK_MS 5000                                    //  Chống ghi đè giá trị cũ từ Web 5s khi nhấn trên ESP32-CYD để tránh giá trị bị nhảy
        static uint32_t limit_lock_ms[P_COUNT]  = {0};          //  Khóa thời gian thiết lập các ngưỡng giá trị
        static uint32_t manual_lock_ms[P_COUNT] = {0};          //  Khóa thời gian các lệnh điều khiển thủ công
        static uint32_t name_lock_ms            = 0;            //  Khóa thời gian đổi tên thiết bị
        static bool     cloud_push_now          = false;        //  Khi được nhấn/thay đổi giá trị,... Gửi liền lên web ko cần chờ chu kì
        //  Hàm chống ghi đè
        static inline bool locked(uint32_t stamp) { return (millis() - stamp) < LOCK_MS; }


//  PHẦN 2: KHAI BÁO HÀM
    //  Các hàm xài tùm lum
        //  Hàm kẹp giá trị(giới hạn trong khoảng) 
        static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

        //  Ứng dụng hàm kẹp để giới hạn vật lí
        static inline float clamp_phys(int i, float v) {
            if (i < 0 || i >= P_COUNT) return v;
            return clampf(v, PHYS_MIN[i], PHYS_MAX[i]);
        }

        //  Hàm tạo số ngẫu nhiên trong khoảng a, b 
        static float frand(float a, float b) __attribute__((unused));   //  Trình biên dịch sẽ ko báo cảnh báo khi ở chế độ đọc thực tế
        static float frand(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }

        //  Các thông số PH và nhiệt độ làm tròn 1 chữ số, còn lại làm tròn hết
        static void fmt_val(int p, float v, char* buf, size_t n) {
            if (p == P_PH || p == P_AIR_TEMP || p == P_WATER_TEMP) snprintf(buf, n, "%.1f", v);
            else                                                   snprintf(buf, n, "%.0f", v);
        }

        //  Hàm vẫn đọc giá trị cảm biến lúc bật chế độ thủ công
        static inline bool param_eff(int crop_idx, int i, float* out) {
            if (crop_idx < 0 || crop_idx >= (int)crops.size()) return false;
            if (crop_idx == active_crop && sensor_valid[i]) { *out = sensor_value[i]; return true; }
            return false;
        }

        //  Hàm đọc giới hạn trên từ cây được chọn
        static inline float get_lo_limit(int c, int i) {
            // Nếu chỉ số cây 'c' hợp lệ (đã được chọn trong danh sách)
            if (c >= 0 && c < (int)crops.size()) {
                return crops[c].lo_limit[i]; // Trả về ngưỡng dưới riêng của cây đó
            } else {
                return param_meta[i].lo_limit; // Trả về ngưỡng dưới mặc định của hệ thống
            }
        }

        //  Hàm đọc giới hạn dưới từ cây được chọn
        static inline float get_hi_limit(int c, int i) {
            // Nếu chỉ số cây 'c' hợp lệ (đã được chọn trong danh sách)
            if (c >= 0 && c < (int)crops.size()) {
                return crops[c].hi_limit[i]; // Trả về ngưỡng trên riêng của cây đó
            } else {
                return param_meta[i].hi_limit; // Trả về ngưỡng trên mặc định của hệ thống
            }
        }

        //  Hàm ghi giới hạn trên vào cây được chọn
        static inline void set_lo_limit(int c, int i, float v) {
            // Nếu chỉ số cây 'c' hợp lệ, cập nhật ngưỡng dưới mới cho cây đó
            if (c >= 0 && c < (int)crops.size()) {
                crops[c].lo_limit[i] = v;
            }
        }

        //  Hàm ghi giới hạn dưới vào cây được chọn
        static inline void set_hi_limit(int c, int i, float v) {
            // Nếu chỉ số cây 'c' hợp lệ, cập nhật ngưỡng trên mới cho cây đó
            if (c >= 0 && c < (int)crops.size()) {
                crops[c].hi_limit[i] = v;
            }
        }

        //  Hàm chuyển các keys thành chỉ số(ngược lại cái trên)
        static int sensor_key_to_index(const char* key) {
            for (int i = 0; i < P_COUNT; i++) if (!strcmp(key, SENSOR_KEYS[i])) return i;
            return -1;
        }



    // Sensor.h
        static void sensor_set_source(uint8_t mode);
        static void sensor_push_limits();

    // Wifi.h
        static void wifi_connect_blocking(uint32_t timeout_ms);

    // Ui.h + UiHome/UiDetail/UiSettings
        static void ui_init();
        static void ui_rebuild_tiles();
        static void ui_goto_page(int idx, bool anim);
        static void ui_open_detail(int crop_idx, int param_idx);
        static void ui_open_crop_modal(int crop_idx);   // crop_idx < 0 = thêm cây mới
        static void ui_open_settings();
        static void ui_pump_sync_switches();
        static void ui_alarm_update_overlay();
        static void ui_alarm_clear(int crop_idx, int param_idx);
        static void ui_detail_refresh_limits();         // vẽ lại ngưỡng + panel tay (nếu đang mở)
        static void ui_apply_remote_name(const char* name);
        static void ui_apply_remote_limits(const char* json);
        static void ui_apply_remote_pump(const char* json);


//  Phần 3: GHI/ĐỌC DỮ LIỆU TỪ THẺ NHỚ/EPPROM
    //  3.1 Cấu hình thẻ nhớ/EPPROM
        #define SD_CS_PIN        5                      //  Cấu hình chân CS điều khiển thẻ SD
        #define SD_SPI_HZ        4000000                //  Cấu hình tần số SPI
        #define SD_CONFIG_PATH   "/farm_config.json"    //  Tên File cài đặt trong thẻ nhớ, cấu hình các cây trồng
        #define SD_CSV_PATH      "/data_current.csv"    //  File ghi dữ liệu đo

        static bool        sd_ready = false;            //  Cờ kiểm tra thẻ nhớ hoạt động
        static bool        nvs_open = false;            //  Cờ kiểm tra EPPROM hoạt động
        static             Preferences nvs;             //  Cấu hình EPPROM ghi dữ liệu

        //  Hàm mở vùng nhớ "farmcfg" EPPROM
        static void nvs_open_if_needed() {
            if (!nvs_open) { 
                nvs.begin("farmcfg", false); 
                nvs_open = true; 
            }
        }

        // Thẻ còn đọc được không (phát hiện rút thẻ giữa chừng).
        static bool sd_still_alive() {
            File root = SD.open("/");
            if (!root) return false;
            root.close();
            return true;
        }

        
        static void storage_init() {
            sd_ready = SD.begin(SD_CS_PIN, screen.touchSPI(), SD_SPI_HZ) && SD.cardType() != CARD_NONE;
            if (!sd_ready) nvs_open_if_needed();
            Serial.println(sd_ready ? "[Storage] Co the SD -> luu ra file."
                                    : "[Storage] Khong co the SD -> luu vao NVS.");
        }

    // 3.2  Ghi
        //  Ghi cấu hình theo chuổi JSON
            static void storage_save_config() {
                JsonDocument doc;
                doc["selected"]    = active_crop;
                doc["page"]        = current_page;
                doc["device_name"] = device_name;
                doc["link"]        = sensor_source;
                doc["peer"]        = sensor_mac;
                doc["pump_sw"]     = pump_on;

                JsonObject net = doc["net"].to<JsonObject>();
                net["room"] = mqtt_room_id;
                net["ssid"] = wifi_ssid;
                net["pass"] = wifi_pass;
                net["tg"]   = telegram_chat_id;

                JsonArray plants = doc["plants"].to<JsonArray>();
                for (size_t i = 0; i < crops.size(); i++) {
                    JsonObject po = plants.add<JsonObject>();
                    po["name"] = crops[i].name;
                    po["logo"] = crops[i].logo_idx;
                    JsonArray sp   = po["setpoint"].to<JsonArray>();
                    JsonArray val  = po["value"].to<JsonArray>();
                    JsonArray lo   = po["lo"].to<JsonArray>();
                    JsonArray hi   = po["hi"].to<JsonArray>();
                    JsonArray man  = po["man"].to<JsonArray>();
                    JsonArray mset = po["mset"].to<JsonArray>();
                    JsonArray sw1  = po["sw1"].to<JsonArray>();
                    JsonArray sw2  = po["sw2"].to<JsonArray>();
                    for (int k = 0; k < P_COUNT; k++) {
                        sp.add(crops[i].setpoint[k]);
                        val.add(crops[i].value[k]);
                        lo.add(crops[i].lo_limit[k]);
                        hi.add(crops[i].hi_limit[k]);
                        man.add(crops[i].manual[k]);
                        mset.add(crops[i].manual_val_set[k]);
                        sw1.add(crops[i].sw1[k]);
                        sw2.add(crops[i].sw2[k]);
                    }
                }

        //  Hàm chọn ghi vào thẻ nhớ hay vào EPPROM
            if (sd_ready) {
                SD.remove(SD_CONFIG_PATH);
                File f = SD.open(SD_CONFIG_PATH, FILE_WRITE);
                if (f) { 
                    serializeJson(doc, f); 
                    f.close(); }
                else     
                Serial.println("[Storage] Loi: khong ghi duoc file cau hinh!");
            } 
            
            else {
                nvs_open_if_needed();
                String out;
                serializeJson(doc, out);
                nvs.putString("cfg", out);
            }
        }

    //  3.3 Đọc
        //  Load cấu hình trong lần đầu khởi động, cái nào không có dùng giá trị mặc định
            static void storage_load_config() {
                String raw;
                if (sd_ready) {
                    if (SD.exists(SD_CONFIG_PATH)) {
                        File f = SD.open(SD_CONFIG_PATH, FILE_READ);
                        if (f) { raw = f.readString(); f.close(); }
                    }
                } else {
                    nvs_open_if_needed();
                    raw = nvs.getString("cfg", "");
                }
                if (raw.length() == 0) return;

                JsonDocument doc;
                if (deserializeJson(doc, raw) != DeserializationError::Ok) {
                    Serial.println("[Storage] File cau hinh hong -> bo qua, dung mac dinh.");
                    return;
                }


                //
                float legacy_lo[P_COUNT], legacy_hi[P_COUNT];
                for (int i = 0; i < P_COUNT; i++) {
                    legacy_lo[i] = param_meta[i].lo_limit;
                    legacy_hi[i] = param_meta[i].hi_limit;
                }
                if (doc["limits"].is<JsonArray>()) {
                    int i = 0;
                    for (JsonObject lo : doc["limits"].as<JsonArray>()) {
                        if (i >= P_COUNT) break;
                        legacy_lo[i] = lo["lo"] | legacy_lo[i];
                        legacy_hi[i] = lo["hi"] | legacy_hi[i];
                        i++;
                    }
                }

                if (doc["plants"].is<JsonArray>()) {
                    crops.clear();
                    for (JsonObject po : doc["plants"].as<JsonArray>()) {
                        Crop p;
                        memset(&p, 0, sizeof(p));
                        strncpy(p.name, (const char*)(po["name"] | "Crop"), sizeof(p.name) - 1);

                        int lidx = po["logo"] | 0;
                        if (lidx < 0 || lidx >= NUM_CROP_LOGOS) lidx = 0;
                        p.logo_idx = lidx;
                        p.logo     = CROP_LOGOS[lidx];

                        JsonArray sp = po["setpoint"], val = po["value"];
                        JsonArray lo = po["lo"],       hi  = po["hi"];
                        JsonArray man = po["man"],     mset = po["mset"];
                        JsonArray sw1 = po["sw1"],     sw2  = po["sw2"];

                        for (int k = 0; k < P_COUNT; k++) {
                            p.setpoint[k] = (sp  && k < (int)sp.size())  ? sp[k].as<float>()  : LETTUCE_SETPOINT[k];
                            p.value[k]    = (val && k < (int)val.size()) ? val[k].as<float>() : p.setpoint[k];

                            // Kẹp vật lý NGAY LÚC NẠP: file cũ hoặc file sửa tay có thể chứa pH hi = 13.
                            p.lo_limit[k] = clamp_phys(k, (lo && k < (int)lo.size()) ? lo[k].as<float>() : legacy_lo[k]);
                            p.hi_limit[k] = clamp_phys(k, (hi && k < (int)hi.size()) ? hi[k].as<float>() : legacy_hi[k]);
                            if (p.lo_limit[k] > p.hi_limit[k]) {
                                float t = p.lo_limit[k]; p.lo_limit[k] = p.hi_limit[k]; p.hi_limit[k] = t;
                            }

                            p.manual[k]         = (man  && k < (int)man.size())  ? man[k].as<bool>()  : false;
                            p.manual_val_set[k] = (mset && k < (int)mset.size()) ? mset[k].as<bool>() : false;
                            p.sw1[k]            = (sw1  && k < (int)sw1.size())  ? sw1[k].as<bool>()  : false;
                            p.sw2[k]            = (sw2  && k < (int)sw2.size())  ? sw2[k].as<bool>()  : false;
                        }
                        crops.push_back(p);
                    }
                }

                active_crop  = doc["selected"] | 0;
                current_page = doc["page"]     | 0;
                pump_on      = doc["pump_sw"]  | false;

                strncpy(device_name, (const char*)(doc["device_name"] | device_name), sizeof(device_name) - 1);
                device_name[sizeof(device_name) - 1] = '\0';

                int lm = doc["link"] | (int)SOURCE_RADIO;
                sensor_source = (lm == SOURCE_SIM) ? SOURCE_SIM : SOURCE_RADIO;

                strncpy(sensor_mac, (const char*)(doc["peer"] | ""), sizeof(sensor_mac) - 1);
                sensor_mac[sizeof(sensor_mac) - 1] = '\0';

                if (doc["net"].is<JsonObject>()) {
                    JsonObject net = doc["net"];
                    strncpy(wifi_ssid, (const char*)(net["ssid"] | ""), sizeof(wifi_ssid) - 1);
                    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
                    strncpy(wifi_pass, (const char*)(net["pass"] | ""), sizeof(wifi_pass) - 1);
                    wifi_pass[sizeof(wifi_pass) - 1] = '\0';
                    strncpy(telegram_chat_id, (const char*)(net["tg"] | ""), sizeof(telegram_chat_id) - 1);
                    telegram_chat_id[sizeof(telegram_chat_id) - 1] = '\0';
                }

                // room KHÔNG đọc từ file — tính lại từ Chat ID mỗi lần boot. Tự chữa lành khi đổi SALT
                // hoặc khi file config còn room kiểu cũ.
                String room = deriveRoomId(String(telegram_chat_id));
                strncpy(mqtt_room_id, room.c_str(), sizeof(mqtt_room_id) - 1);
                mqtt_room_id[sizeof(mqtt_room_id) - 1] = '\0';

                if (crops.empty() || active_crop >= (int)crops.size()) active_crop = 0;
                if (current_page > (int)crops.size())                  current_page = active_crop;
            }





        //  Hàm xóa file trên thẻ nhớ/EPPROM
        static void storage_clear_all() {
            if (sd_ready) {
                SD.remove(SD_CONFIG_PATH);
                SD.remove(SD_CSV_PATH);
            } else {
                nvs_open_if_needed();
                nvs.remove("cfg");
            }
            Serial.println("[Storage] Da xoa cau hinh + file du lieu cua app.");
        }

    //  3.4 Xuất CSV
        //  Ghi File CSV vào thẻ nhớ
            static void storage_csv_export_cb(lv_timer_t* t) {
                if (!sd_ready) 
                    return;
                if (active_crop < 0 || active_crop >= (int)crops.size()) 
                    return;

                Crop& pl = crops[active_crop];
                SD.remove(SD_CSV_PATH);
                File f = SD.open(SD_CSV_PATH, FILE_WRITE);
                if (!f) { 
                    Serial.println("[Storage] Loi: khong ghi duoc CSV!"); 
                    return; }

                f.println("Crop,Parameter,Unit,CurrentValue,Setpoint,LowerLimit,UpperLimit");
                for (int i = 0; i < P_COUNT; i++) {
                    ParamMeta& m = param_meta[i];
                    f.printf("%s,%s,%s,%.2f,%.2f,%.2f,%.2f\n",
                            pl.name, m.full_name, m.unit,
                            pl.value[i], pl.setpoint[i], m.lo_limit, m.hi_limit);
                }
                f.close();
            }

        //  Hàm kiểm tra có thẻ nhớ cắm/rút 
            static void storage_hotplug_check_cb(lv_timer_t* t) {
                if (sd_ready) {
                    if (!sd_still_alive()) {
                        sd_ready = false;
                        nvs_open_if_needed();
                        Serial.println("[Storage] The nho VUA BI RUT -> chuyen sang NVS.");
                    }
                } else if (SD.begin(SD_CS_PIN, screen.touchSPI(), SD_SPI_HZ) && SD.cardType() != CARD_NONE) {
                    sd_ready = true;
                    Serial.println("[Storage] Phat hien the nho MOI CAM -> chuyen sang SD.");
                    storage_save_config();
                }
            }
