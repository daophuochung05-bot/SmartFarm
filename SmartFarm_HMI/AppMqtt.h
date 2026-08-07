#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include <vector>
#include "AppData.h"
#include "AppSensor.h"

#define MQTT_BROKER       "broker.hivemq.com"               // Địa chỉ Server trung gian
#define MQTT_PORT         1883                              // Cổng kết nối không bảo mật của giao thức MQTT
#define MQTT_TOPIC_BASE  "LayerMaker/SmartFarm/farm"        // Đường dẫn để gửi nhận dữ liệu theo giao thức MQTT

#define MQTT_HEARTBEAT_MS  4000UL                           // Cứ 4 giây gửi 1 nhịp tim báo còn online
#define MQTT_RECONNECT_MS  2000UL                           // Rớt mạng thì cứ 2 giây thử nối lại broker
#define MQTT_QUEUE_CAP     24                               // Sức chứa hàng đợi lệnh phản hồi từ ESP32 CYD lên Web khi mất WiFi

#define CLOUD_PUBLISH_MS   2500                             // Cứ 2.5 giây đẩy toàn bộ trạng thái lên web
#define CLOUD_CMD_POLL_MS  200                              // Cứ 0.2 giây kiểm tra xem web có gửi lệnh xuống không
#define TIMEZONE_OFFSET_SEC (7 * 3600)                      // Múi giờ GMT+7, để lấy giờ thật qua NTP


// Phần 1: Các hàm chạy bên dưới Core 0
    class MqttCloud {
    public:
        void begin(const char* ssid, const char* pass,
                const char* deviceName, const char* version, const char* roomId);

        // Đổi tên thiết bị, tự reconnect để LWT cập nhật tên mới tránh báo offline sai tên
        void setIdentity(const char* deviceName, const char* version);

        bool isWiFiConnected() { return WiFi.status() == WL_CONNECTED; }
        bool isMqttConnected() { return _mqtt.connected(); }
        const char* mac() const { return _mac.c_str(); }

        // Các hàm giao tiếp dành cho luồng giao diện LVGL gọi
        void publishFull(const String& json);   // Gửi trạng thái tổng hợp, gói mới nhất sẽ ghi đè gói cũ
        void publishAck(const String& json);    // Gửi phản hồi kết quả thực hiện lệnh về cho Web
        bool popCommand(String& out);           // Lấy lệnh từ Web ra xử lý theo thứ tự vào trước ra trước

        void loopTask();                        // Hàm chạy vòng lặp ngầm cho Task, không gọi thủ công

    private:
        String _ssid, _pass, _version, _name, _roomId, _mac;
        String _topicStatus, _topicFull, _topicCmd, _topicAck, _clientId;

        bool          _wantWiFi      = false;
        volatile bool _identityDirty = false;
        uint32_t      _lastBeat = 0, _lastTry = 0;

        String              _fullPending;
        bool                _fullDirty = false;
        std::vector<String> _ackQueue;
        std::vector<String> _cmdQueue;

        WiFiClient        _net;
        PubSubClient      _mqtt{_net};
        SemaphoreHandle_t _mutex = nullptr;
        TaskHandle_t      _task  = nullptr;

        bool   _connectMqtt();
        void   _drainOutbox();
        String _buildStatus(bool online);

    public:
        void pushCommand(const String& msg);    // Hàm callback mạng gọi để nhét lệnh mới nhận vào hàng đợi
    };

    static MqttCloud  mqttCloud;                // Tạo một đối tượng MQTT duy nhất cho toàn bộ hệ thống
    static MqttCloud* mqtt_self = nullptr;      // Con trỏ toàn cục để hàm callback hệ thống truy cập vào đối tượng MQTT

    // Hàm tự mã hóa các ký tự đặc biệt khi dựng chuỗi JSON thủ công
    static void jsonEscapeAppend(String& out, const String& s) {
        for (size_t i = 0; i < s.length(); i++) {
            char c = s[i];
            switch (c) {
                case '\"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if ((uint8_t)c < 0x20) { char b[7]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
                    else                   out += c;
            }
        }
    }

    // Đọc địa chỉ MAC định danh duy nhất được lưu trong chip ESP32
    static String readEfuseMac() {
        uint64_t mac = ESP.getEfuseMac();
        char buf[13];
        snprintf(buf, sizeof(buf), "%04X%08X",
                (unsigned)((mac >> 32) & 0xFFFF), (unsigned)(mac & 0xFFFFFFFF));
        return String(buf);
    }

    // Hàm Callback tự động kích hoạt khi có gói tin MQTT chứa lệnh điều khiển truyền xuống
    inline void mqtt_message_cb(char* topic, byte* payload, unsigned int len) {
        if (!mqtt_self) return;
        String msg;
        msg.reserve(len);
        for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
        mqtt_self->pushCommand(msg);
    }

    // Cầu nối giúp FreeRTOS thực thi hàm C++ thành một Task độc lập
    inline void mqtt_task_runner(void* pv) {
        static_cast<MqttCloud*>(pv)->loopTask();
        vTaskDelete(nullptr);
    }

    // Khởi tạo thông tin mạng, tạo các Topic động và kích hoạt Task chạy ngầm ở Core 0
    inline void MqttCloud::begin(const char* ssid, const char* pass,
                                const char* deviceName, const char* version, const char* roomId) {
        mqtt_self = this;

        if (ssid) _ssid = ssid; else _ssid = "";
        if (pass) _pass = pass; else _pass = "";
        if (deviceName && deviceName[0]) _name = deviceName; else _name = "SmartFarm";
        if (version) _version = version; else _version = "1.0";
        _wantWiFi = _ssid.length() > 0;
        _mutex    = xSemaphoreCreateMutex(); // Khởi tạo khóa bảo vệ tránh xung đột bộ nhớ giữa 2 Core

        if (roomId && roomId[0]) _roomId = roomId;
        else                     _roomId = "unset";
        if (_roomId == "unset")
            Serial.println("[Mqtt] CANH BAO: chua co Telegram ID, vao phong chung unset");

        _mac = readEfuseMac();
        String base  = String(MQTT_TOPIC_BASE) + "/" + _roomId + "/" + _mac;
        _topicStatus = base + "/status";
        _topicFull   = base + "/full";
        _topicCmd    = base + "/cmd";
        _topicAck    = base + "/ack";
        _clientId    = "farm_" + _roomId + "_" + _mac;

        _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
        _mqtt.setCallback(mqtt_message_cb);
        
        _mqtt.setKeepAlive(30);
        _mqtt.setBufferSize(3072);      // Cấp phát bộ đệm lớn để chứa vừa chuỗi JSON trạng thái đầy đủ
        _mqtt.setSocketTimeout(4);

        Serial.printf("[Mqtt] mac=%s room=%s\n", _mac.c_str(), _roomId.c_str());

        // Tạo luồng xử lý mạng riêng biệt chạy tại Core 0 với dung lượng RAM 16KB
        if (!_task) xTaskCreatePinnedToCore(mqtt_task_runner, "MqttCloud", 16384, this, 1, &_task, 0);
    }

    // Cập nhật tên thiết bị và dựng cờ yêu cầu reconnect để cập nhật lại thông tin Last Will
    inline void MqttCloud::setIdentity(const char* deviceName, const char* version) {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
        if (version && version[0]) _version = version;
        if (deviceName && deviceName[0] && _name != deviceName) {
            _name = deviceName;
            _identityDirty = true;
        }
        xSemaphoreGive(_mutex);
    }

    // Nhận chuỗi trạng thái đầy đủ từ luồng giao diện để chuẩn bị đẩy lên Cloud
    inline void MqttCloud::publishFull(const String& json) {
        if (json.length() == 0) return;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
        _fullPending = json;   // Gói mới nhất sẽ ghi đè lên gói cũ đang chờ
        _fullDirty   = true;   // Dựng cờ báo hiệu có dữ liệu mới cần gửi đi
        xSemaphoreGive(_mutex);
    }

    // Thêm chuỗi phản hồi kết quả lệnh vào hàng đợi để chuẩn bị gửi trả về cho Web
    inline void MqttCloud::publishAck(const String& json) {
        if (json.length() == 0) return;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
        if (_ackQueue.size() < MQTT_QUEUE_CAP) _ackQueue.push_back(json);
        xSemaphoreGive(_mutex);
    }

    // Nhận lệnh thô từ mạng và nhét vào hàng đợi cho luồng màn hình bốc ra xử lý sau
    inline void MqttCloud::pushCommand(const String& msg) {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
        if (_cmdQueue.size() < MQTT_QUEUE_CAP) _cmdQueue.push_back(msg);
        xSemaphoreGive(_mutex);
    }

    // Luồng màn hình gọi hàm này để lấy lệnh ra xử lý theo thứ tự xếp hàng
    inline bool MqttCloud::popCommand(String& out) {
        bool got = false;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (!_cmdQueue.empty()) {
                out = _cmdQueue.front();
                _cmdQueue.erase(_cmdQueue.begin());
                got = true;
            }
            xSemaphoreGive(_mutex);
        }
        return got;
    }

    // Tự lắp ráp chuỗi JSON báo cáo trạng thái kết nối Online hoặc Offline của thiết bị
    inline String MqttCloud::_buildStatus(bool online) {
        String name = "SmartFarm", version;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            name    = _name;
            version = _version;
            xSemaphoreGive(_mutex);
        }

        String p = "{";
        p += "\"mac\":\"" + _mac + "\",";
        p += "\"id\":\"";  jsonEscapeAppend(p, name); p += "\",";
        if (online) {
            p += "\"on\":true,";
            p += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
            p += "\"ver\":\""; jsonEscapeAppend(p, version); p += "\",";
            p += "\"up\":" + String(millis());
        } else {
            p += "\"on\":false";
        }
        p += "}";
        return p;
    }

    // Vòng lặp vĩnh cửu chạy ngầm ở Core 0 quản lý kết nối WiFi, MQTT và xả hàng đợi dữ liệu
    inline void MqttCloud::loopTask() {
        for (;;) {
            if (_wantWiFi && WiFi.status() != WL_CONNECTED) {
                Serial.printf("[Mqtt] Noi lai WiFi: %s\n", _ssid.c_str());
                WiFi.mode(WIFI_STA);
                WiFi.begin(_ssid.c_str(), _pass.c_str());
                for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++)
                    vTaskDelay(pdMS_TO_TICKS(500));
                if (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(5000));
            }

            if (WiFi.status() == WL_CONNECTED) {
                if (!_mqtt.connected()) {
                    if (millis() - _lastTry >= MQTT_RECONNECT_MS) {
                        _lastTry = millis();
                        _connectMqtt();
                    }
                } else if (_identityDirty) {
                    _identityDirty = false;
                    _mqtt.disconnect();         // Ngắt kết nối để ép hệ thống nạp lại thông tin Last Will mới
                    _lastTry = 0;
                } else {
                    _mqtt.loop();
                    if (millis() - _lastBeat >= MQTT_HEARTBEAT_MS) {
                        _lastBeat = millis();
                        _mqtt.publish(_topicStatus.c_str(), _buildStatus(true).c_str(), true);
                    }
                    _drainOutbox();             // Lấy dữ liệu ra khỏi hàng đợi và gửi lên mạng
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));      // Tạm dừng 20ms để giải phóng CPU cho các Task khác cùng Core
        }
    }

    // Thực hiện bắt tay với Broker MQTT, cài đặt di chúc Last Will và subscribe nhận lệnh điều khiển
    inline bool MqttCloud::_connectMqtt() {
        String will = _buildStatus(false);

        if (!_mqtt.connect(_clientId.c_str(), _topicStatus.c_str(),
                        1, true, will.c_str())) {
            Serial.printf("[Mqtt] Connect that bai, state=%d\n", _mqtt.state());
            return false;
        }

        _mqtt.publish(_topicStatus.c_str(), _buildStatus(true).c_str(), true);
        _mqtt.subscribe(_topicCmd.c_str(), 1);
        _lastBeat = millis();

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_fullPending.length() > 0) _fullDirty = true;
            xSemaphoreGive(_mutex);
        }

        Serial.printf("[Mqtt] Connected nghe lenh tren %s\n", _topicCmd.c_str());
        return true;
    }

    // Trích xuất an toàn các gói tin đang nằm trong hàng đợi trung gian rồi đẩy thẳng lên MQTT
    inline void MqttCloud::_drainOutbox() {
        String              fullJob;
        bool                haveFull = false;
        std::vector<String> acks;

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_fullDirty) { fullJob = _fullPending; _fullDirty = false; haveFull = true; }
            if (!_ackQueue.empty()) acks.swap(_ackQueue); // Hoán đổi vùng nhớ cực nhanh để giải phóng hàng đợi dưới Mutex
            xSemaphoreGive(_mutex);
        }

        // Tiến hành gửi thực tế, không giữ khóa Mutex để tránh làm treo luồng hiển thị màn hình
        if (haveFull) _mqtt.publish(_topicFull.c_str(), fullJob.c_str(), true);
        for (auto& a : acks) _mqtt.publish(_topicAck.c_str(), a.c_str(), false);
    }


// Air Phần 2: Các hàm chạy bên trên luồng LVGL Core 1
static bool cloud_ready = false;

// Đóng gói toàn bộ các thông số đo đạc, ngưỡng giới hạn cài đặt của nông trại thành định dạng JSON
static String cloud_build_full_json() {
    JsonDocument doc;
    doc["id"]   = device_name;
    doc["ver"]  = FIRMWARE_VERSION;
    doc["ip"]   = WiFi.localIP().toString();
    doc["up"]   = (uint32_t)millis();
    doc["link"] = sensor_source;
    
    if (sensor_source == SOURCE_SIM) doc["src"] = "sim";
    else                             doc["src"] = "espnow";

    JsonObject sensor = doc["sensor"].to<JsonObject>();
    if (sensorRadio.hasPeer()) sensor["smac"] = sensorRadio.peerMacStr();
    else                       sensor["smac"] = String("--");
    sensor["ok"]   = sensorRadio.hasPeer();
    sensor["ch"]   = sensorRadio.channel();
    sensor["rx"]   = sensorRadio.rxCount();
    
    if (sensor_last_rx_ms) sensor["age"] = (uint32_t)((millis() - sensor_last_rx_ms) / 1000);
    else                   sensor["age"] = (uint32_t)9999;

    int  crop  = active_crop;
    bool valid = (crop >= 0 && crop < (int)crops.size());
    if (valid) doc["plant"] = crops[crop].name;

    JsonObject params = doc["params"].to<JsonObject>();
    for (int i = 0; i < P_COUNT; i++) {
        JsonObject o = params[SENSOR_KEYS[i]].to<JsonObject>();
        o["name"] = param_meta[i].full_name;
        o["unit"] = param_meta[i].unit;

        float value;
        if (valid && param_eff(crop, i, &value)) o["value"] = value;
        else                                     o["value"] = nullptr;

        if (valid) {
            o["lo"]  = crops[crop].lo_limit[i];
            o["hi"]  = crops[crop].hi_limit[i];
            o["man"] = crops[crop].manual[i];
            o["sw1"] = crops[crop].sw1[i];
            o["sw2"] = crops[crop].sw2[i];
            if (crops[crop].manual[i] && crops[crop].manual_val_set[i])
                o["mv"] = crops[crop].value[i];
        }
    }

    doc["pump"]["sw"] = pump_on;

    String out;
    serializeJson(doc, out);
    return out;
}

// Hàm callback chạy theo chu kỳ của LVGL để đẩy gói dữ liệu tổng hợp lên Cloud và cấu hình giờ NTP
static void cloud_publish_full_cb(lv_timer_t* t) {
    if (!cloud_ready) return;

    static bool time_configured = false;
    if (!time_configured && mqttCloud.isWiFiConnected()) {
        configTime(TIMEZONE_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
        time_configured = true;
    }

    mqtt_online = mqttCloud.isMqttConnected();
    mqttCloud.setIdentity(device_name, FIRMWARE_VERSION);
    mqttCloud.publishFull(cloud_build_full_json());
}

// Bóc tách chuỗi lệnh JSON nhận được từ Web xuống để áp đặt thay đổi cấu hình hệ thống phần cứng
static void cloud_apply_command(const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        mqttCloud.publishAck("{\"ok\":false,\"msg\":\"bad json\"}");
        return;
    }

    const char* cmd = doc["cmd"] | "";
    bool   ok  = true;
    String msg = "OK";

    if (!strcmp(cmd, "set_name")) {
        const char* name = doc["name"] | "";
        if (!name[0]) { ok = false; msg = "empty name"; }
        else {
            name_lock_ms = 0;
            ui_apply_remote_name(name);
        }

    } else if (!strcmp(cmd, "set_limit") || !strcmp(cmd, "set_manual") || !strcmp(cmd, "set_sw")) {
        const char* key = doc["key"] | "";
        int idx;
        if (key[0]) idx = sensor_key_to_index(key);
        else        idx = -1;

        if (idx < 0) { ok = false; msg = "unknown key"; }
        else {
            limit_lock_ms[idx]  = 0;
            manual_lock_ms[idx] = 0;

            JsonDocument out;
            JsonObject   o = out[key].to<JsonObject>();
            if (!doc["lo"].isNull())  o["lo"]  = doc["lo"].as<float>();
            if (!doc["hi"].isNull())  o["hi"]  = doc["hi"].as<float>();
            if (!doc["man"].isNull()) o["man"] = doc["man"].as<bool>();
            if (!doc["mv"].isNull())  o["mv"]  = doc["mv"].as<float>();
            if (!doc["sw1"].isNull()) o["sw1"] = doc["sw1"].as<bool>();
            if (!doc["sw2"].isNull()) o["sw2"] = doc["sw2"].as<bool>();

            String json;
            serializeJson(out, json);
            ui_apply_remote_limits(json.c_str());
        }

    } else if (!strcmp(cmd, "set_link")) {
        if (doc["mode"].isNull()) { ok = false; msg = "thieu mode"; }
        else {
            int mode = doc["mode"].as<int>();
            if (mode != 0 && mode != 1) { ok = false; msg = "mode phai 0 hoac 1"; }
            else {
                sensor_set_source((uint8_t)mode);
                if (mode) msg = "sim";
                else      msg = "espnow";
            }
        }

    } else if (!strcmp(cmd, "set_pump")) {
        pump_lock_ms = 0;

        JsonDocument out;
        if (!doc["sw"].isNull()) out["sw"] = doc["sw"].as<bool>();
        String json;
        serializeJson(out, json);
        ui_apply_remote_pump(json.c_str());

    } else if (!strcmp(cmd, "reboot")) {
        mqttCloud.publishAck("{\"cmd\":\"reboot\",\"ok\":true,\"msg\":\"khoi dong lai...\"}");
        Serial.println("[Mqtt] Web yeu cau REBOOT.");
        delay(1000);
        ESP.restart();

    } else {
        ok  = false;
        msg = "unknown cmd";
    }

    JsonDocument ack;
    ack["cmd"] = cmd;
    ack["ok"]  = ok;
    ack["msg"] = msg;
    String json;
    serializeJson(ack, json);
    mqttCloud.publishAck(json);

    cloud_push_now = true;      // Kích hoạt cờ yêu cầu đẩy ngay lập tức gói trạng thái mới lên Web
}

// Hàm định thời lấy các lệnh điều khiển điều tiết từ hàng đợi ra ngoài để xử lý
static void cloud_poll_commands_cb(lv_timer_t* t) {
    if (!cloud_ready) return;
    String cmd;
    while (mqttCloud.popCommand(cmd)) cloud_apply_command(cmd);
}

// Khởi chạy hệ thống mạng kết nối MQTT và thiết lập các bộ định thời chu kỳ của LVGL
static void mqtt_start() {
    mqttCloud.begin(wifi_ssid, wifi_pass, device_name, FIRMWARE_VERSION, mqtt_room_id);
    cloud_ready = true;
    
    lv_timer_create(cloud_publish_full_cb,  CLOUD_PUBLISH_MS,  NULL);
    lv_timer_create(cloud_poll_commands_cb, CLOUD_CMD_POLL_MS, NULL);
}