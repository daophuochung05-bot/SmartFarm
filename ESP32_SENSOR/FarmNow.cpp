#include "FarmNow.h"

// Con trỏ tĩnh giúp callback dạng C truy cập đúng đối tượng FarmNow hiện tại.
FarmNow* FarmNow::_self = nullptr;

// Địa chỉ broadcast FF:FF:FF:FF:FF:FF dùng để làm việc với gói quảng bá.
static uint8_t FN_BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/*
  Khởi tạo ESP-NOW ở chế độ Wi-Fi Station nhưng ko kết nối Internet.
  Sau khi sẵn sàng, thiết bị bắt đầu dò kênh từ FN_CH_MIN.
 */
void FarmNow::begin() {
  _self = this;

  // Ko sử dụng WiFi.begin() — con này ko kết nối internet.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();      // chặn nó tự nối lại AP cũ còn lưu trong NVS
  WiFi.setSleep(false);   // STA ngủ giữa beacon = RỚT GÓI ESP-NOW. Bắt buộc tắt.
                          // Nếu ko có dòng này thì sẽ bug: im lặng, ko log gì.

  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] esp_now_init LOI -> reboot");
    delay(200);
    ESP.restart();
  }
  esp_now_register_recv_cb(_recvCb);
  _addPeer(FN_BCAST);

  _setChannel(FN_CH_MIN);
  _lastHop = millis();

  Serial.println();
  Serial.println("========================================");
  Serial.printf ("  MAC SENSOR: %s\n", WiFi.macAddress().c_str());
  Serial.println("  (MAC nay se hien tren web/CYD)");
  Serial.println("========================================");
  Serial.println("[NOW] DO KENH 1..13 tim CYD (khong can SSID)...");
}

// Chuyển kênh Wi-Fi của mạch cảm biến khi đang ở trạng thái dò CYD.
void FarmNow::_setChannel(uint8_t ch) {
  // Con này ko nối AP nào nên sẽ  đc phép tự set kênh. Khác CYD: nó đang
  // nối router, set kênh là rớt WiFi ngay lập tức.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  _ch = ch;
}

// Đăng ký một địa chỉ MAC vào danh sách peer của ESP-NOW.
bool FarmNow::_addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;              // 0 = kênh hiện hành. Điền số cứng ở đây là tự
                              // bắn vào chân mình khi kênh đổi.
  p.ifidx   = WIFI_IF_STA;
  p.encrypt = false;
  return esp_now_add_peer(&p) == ESP_OK;
}

// Chuyển MAC 6 byte của CYD thành chuỗi để in ra Serial và hiển thị.
String FarmNow::macCyd() const {
  if (_state == FN_HOP) return "--";
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
           _cyd[0],_cyd[1],_cyd[2],_cyd[3],_cyd[4],_cyd[5]);
  return String(b);
}

// Hủy peer hiện tại và quay về trạng thái dò kênh khi mất liên kết hoặc unpair.
void FarmNow::_gotoHop(const char* why) {
  if (_state != FN_HOP) {
    if (esp_now_is_peer_exist(_cyd)) esp_now_del_peer(_cyd);
    Serial.printf("[NOW] <<< %s -> quay lai DO KENH\n", why);
  }
  _state   = FN_HOP;
  _lastHop = millis();
}

// API cho phép chương trình chủ động hủy ghép và tìm lại CYD.
void FarmNow::unpair() { _gotoHop("nguoi dung yeu cau ghep lai"); }

/*
  Xử lý gói ESP-NOW nhận được trong task WiFi:
  nhận beacon, xác nhận ghép, lệnh bơm và bỏ qua thiết bị ko đúng MAC.
 */
void FarmNow::_onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  // Chặn gói rỗng, quá kích thước hoặc ko có dạng JSON.
  if (len <= 0 || len > FN_MAX_PAYLOAD || data[0] != '{') return;

  // Nhận dạng nhanh hai gói điều khiển ko cần giải mã toàn bộ JSON.
  bool isHb = (len >= 5 && memcmp(data, "{\"hb\"", 5) == 0);
  bool isPa = (len >= 5 && memcmp(data, "{\"pa\"", 5) == 0);

  //  Đang DÒ: nghe thấy beacon -> bám con CYD này, chuyển sang XIN GHÉP
  if (_state == FN_HOP) {
    if (!isHb) return;
    memcpy(_cyd, mac, 6);
    _addPeer(_cyd);
    _state     = FN_PAIRING;
    _pairSince = millis();
    _lastReq   = 0;                  // gửi {"pr":1} ngay vòng update() kế
    _lastRx    = millis();
    Serial.printf("[NOW] Thay CYD %s tren KENH %u -> XIN GHEP.\n", macCyd().c_str(), _ch);
    Serial.println("[NOW] >>> BAM [GHEP] TREN MAN HINH CYD (con 30s) <<<");
    return;
  }

  // Sau khi đã chọn CYD, chỉ chấp nhận gói đến từ đúng MAC đó.
  if (memcmp(_cyd, mac, 6) != 0) return;   // CYD khác / nhiễu -> lơ
  _lastRx = millis();

  if (isPa) {                              // CYD chấp nhận ghép
    if (_state != FN_LINKED) {
      _state = FN_LINKED;
      Serial.printf("[NOW] >>> DA GHEP voi CYD %s (KENH %u). Bat dau gui so.\n",
                    macCyd().c_str(), _ch);
    }
    return;
  }
  if (isHb) return;                        // beacon: chỉ để canh sống, bỏ qua

  if (_state != FN_LINKED) return;         // chưa được xác nhận -> ko nghe gì thêm

  // Sao chép payload vào bộ đệm có ký tự kết thúc để ArduinoJson xử lý an toàn.
  char buf[FN_MAX_PAYLOAD + 1];
  memcpy(buf, data, len);
  buf[len] = '\0';

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, buf)) return;

  // Callback chỉ lưu lệnh bơm; loop() sẽ đọc và tác động relay sau.
  // Lệnh bơm -> để dành cho loop() lấy qua readPumpCommand().
  if (doc.containsKey("pump")) {
    _pumpVal = doc["pump"].as<int>();
    _pumpNew = true;
    return;
  }

  // Gói giới hạn {"ph":{"lo":..,"hi":..}, ...} — hiện ESP32_SENSOR ko dùng tới.
}

// Hai phiên bản callback giúp mã nguồn tương thích Arduino-ESP32 core 2.x và 3.x.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void FarmNow::_recvCb(const esp_now_recv_info_t* info, const uint8_t* d, int l) {
  if (_self && info) _self->_onRecv(info->src_addr, d, l);
}
#else
void FarmNow::_recvCb(const uint8_t* mac, const uint8_t* d, int l) {
  if (_self) _self->_onRecv(mac, d, l);
}
#endif

// Máy trạng thái: dò kênh -> xin ghép -> đã ghép.  gọi mỗi vòng trong loop().
/*
  update() thực hiện máy trạng thái ko chặn:
  dò lần lượt kênh 1..13, xin ghép định kỳ và phát hiện CYD mất tín hiệu.
 */
void FarmNow::update() {
  uint32_t now = millis();

  switch (_state) {

    // FN_HOP: đổi sang kênh kế tiếp sau mỗi FN_HOP_MS.
    case FN_HOP:
      if (now - _lastHop >= FN_HOP_MS) {
        _lastHop = now;
        _setChannel((_ch >= FN_CH_MAX) ? FN_CH_MIN : (uint8_t)(_ch + 1));
        Serial.printf("[NOW] ...do kenh %u\n", _ch);
      }
      break;

    // FN_PAIRING: gửi yêu cầu ghép và chờ xác nhận trên CYD.
    case FN_PAIRING: {
      // Yêu cầu ghép đều đặn. CYD đang hiện popup, chờ người bấm.
      if (now - _lastReq >= FN_PAIR_REQ_MS) {
        _lastReq = now;
        static const char PR[] = "{\"pr\":1}";
        esp_now_send(_cyd, (const uint8_t*)PR, sizeof(PR) - 1);
        Serial.printf("[NOW] xin ghep... (con %lus) — bam [GHEP] tren CYD\n",
                      (unsigned long)((FN_PAIR_WAIT_MS - (now - _pairSince)) / 1000));
      }
      // Ko ai bấm -> con CYD đó ko phải của mình. Bỏ, đi tìm con khác.
      if (now - _pairSince > FN_PAIR_WAIT_MS) _gotoHop("khong ai bam [GHEP]");
      break;
    }
    // FN_LINKED: theo dõi beacon; quá FN_LOST_MS thì quay lại dò kênh.

    case FN_LINKED:
      // CYD đổi WiFi -> đổi kênh -> ta điếc. Beacon 1s/lần là mạch đập; im lặng
      // quá lâu nghĩa là nó ko còn ở kênh này nữa -> dò lại.
      if (now - _lastRx > FN_LOST_MS) _gotoHop("CYD im lang qua lau");
      break;
  }
}

// Trả lệnh bơm mới đúng một lần rồi xóa cờ _pumpNew.
bool FarmNow::readPumpCommand(int& pumpValue) {
  if (!_pumpNew) return false;
  _pumpNew  = false;
  pumpValue = _pumpVal;
  return true;
}
// Làm tròn số đo trước khi đóng gói để giảm độ dài chuỗi JSON.
float FarmNow::_roundTo(float value, int digits) {
  if (isnan(value)) return NAN;
  float factor = 1.0;
  for (int i = 0; i < digits; i++) factor *= 10.0;
  return round(value * factor) / factor;
}

// Bỏ qua field lỗi/NAN; CYD sẽ hiển thị "--" khi ko nhận được key tương ứng.
void FarmNow::_addIfValid(JsonObject obj, const char* key, float value, bool isValid, int digits) {
  if (!isValid || isnan(value)) return;    // cảm biến hỏng hoặc ko được -> ko gửi key đó,
  obj[key] = _roundTo(value, digits);      // CYD sẽ hiện "--".
}

//Đóng gói sáu thông số cảm biến thành JSON và gửi tới CYD đã ghép.
void FarmNow::sendSensorData(const SensorData& data) {
  // Chưa được [GHÉP] thì ko  gửi. 
  if (_state != FN_LINKED) return;

  StaticJsonDocument<256> doc;
  JsonObject obj = doc.to<JsonObject>();

  _addIfValid(obj, "ph",  data.ph,  data.phValid,  1);
  _addIfValid(obj, "tds", data.tds, data.tdsValid, 0);
  _addIfValid(obj, "lux", data.lux, data.luxValid, 0);
  _addIfValid(obj, "hum", data.hum, data.airValid, 1);
  _addIfValid(obj, "air", data.air, data.airValid, 1);
  _addIfValid(obj, "h2o", data.h2o, data.h2oValid, 1);

  // Kiểm tra kích thước sau serialize để ko bị vượt giới hạn 250 byte của ESP-NOW.
  char out[FN_MAX_PAYLOAD + 1];
  size_t n = serializeJson(doc, out, sizeof(out));
  if (n == 0 || n >= sizeof(out)) {
    Serial.println("[NOW] JSON qua dai (>250B) — ESP-NOW khong gui duoc.");
    return;
  }
  // esp_now_send chỉ xếp gói vào hàng đợi Wi-Fi; kết quả ESP_OK nghĩa là nhận lệnh gửi.

  esp_err_t r = esp_now_send(_cyd, (const uint8_t*)out, n);
  Serial.print("Send to CYD: ");
  Serial.print(out);
  Serial.println(r == ESP_OK ? "" : "   [LOI GUI]");
}
