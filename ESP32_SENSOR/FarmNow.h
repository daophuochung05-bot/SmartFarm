#ifndef _FarmNow_h
#define _FarmNow_h
/*
  FarmNow.h — THAY THẾ NGUYÊN LỚP `UART` bằng ESP-NOW.
  CHUỖI JSON GIỮ NGUYÊN 100%. Chỉ đổi ĐƯỜNG TRUYỀN.
 
   BẮT TAY 3 BƯỚC (có XÁC NHẬN trên màn hình CYD) 
    1. CYD broadcast {"hb":1} mỗi giây, trên kênh nó đang nằm.
    2. Con này DÒ KÊNH 1..13. Nghe thấy beacon -> DỪNG nhảy, bám kênh đó,
       gửi {"pr":1} (xin ghép) mỗi giây tới đúng con CYD đó.
    3. CYD POPUP lên màn hình: MAC + đếm ngược 30s -> [GHÉP] / [TỪ CHỐI].
      - Bấm GHÉP -> CYD trả {"pa":1} -> ta KHOÁ, bắt đầu gửi số.
      - Hết giờ / Từ chối -> CYD im lặng -> ta QUAY LẠI DÒ KÊNH, tìm con
         khác. Lặp mãi tới khi có người xác nhận.
 
    VÌ SAO PHẢI CÓ BƯỚC 3: 2 con CYD đặt gần nhau -> con này dò kênh có thể
    đụng NHẦM con hàng xóm rồi ghép bừa. Im lặng. Sai suốt đời. Không ai biết.
 
    KHÔNG CẦN SSID 
    ESP-NOW chỉ nói chuyện được khi HAI BÊN CÙNG KÊNH. CYD là STA nối router
    -> bị router ÉP nằm ở kênh X, KHÔNG được tự đổi (đổi là rớt WiFi -> mất
    MQTT/Firebase/Telegram). Mà kênh X thì đổi xoành xoạch (đổi SSID trên màn
    hình, router tự đổi kênh...). Nên CON NÀY tự đi tìm. Con này KHÔNG nối AP
    -> ĐƯỢC PHÉP tự set kênh thoải mái.
 
   GIỚI HẠN CỨNG 
      Payload ESP-NOW TỐI ĐA 250 byte. JSON 6 thông số ~90B, JSON giới hạn
      ~223B — vừa lọt. Thêm thông số thứ 7 là phải chia gói.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "Sensor_Reading.h"

#define FN_HOP_MS         250     // dừng bao lâu ở mỗi kênh khi đang dò
#define FN_PAIR_REQ_MS    1000    // nhịp gửi {"pr":1} khi đang xin ghép
#define FN_PAIR_WAIT_MS   40000   // chờ người bấm bao lâu rồi bỏ (> 30s popup CYD)
#define FN_LOST_MS        8000    // đã ghép mà im lặng bấy nhiêu -> mất CYD, dò lại
#define FN_CH_MIN         1
#define FN_CH_MAX         13      // 2.4GHz VN/EU: 1..13. Mỹ tới 11, dò thừa vô hại.
#define FN_MAX_PAYLOAD    250

/*
 Ba trạng thái của máy trạng thái ESP-NOW:
 FN_HOP: đang dò kênh; FN_PAIRING: đang xin ghép; FN_LINKED: đã liên kết.
 */
enum FnState { FN_HOP, FN_PAIRING, FN_LINKED };

// Lớp FarmNow truyền bằng ESP-NOW.
class FarmNow {
public:
  void begin();

  // GỌI MỖI VÒNG loop() — máy trạng thái dò kênh / xin ghép / canh mất kết nối.
  // Ko gọi là nằm chết ở kênh 1, ko bao giờ tìm ra CYD.
  void update();

  // nếu vừa nhận {"pump":x} từ CYD.
  bool readPumpCommand(int& pumpValue);

  // Chưa được [GHÉP] -> im lặng bỏ qua.
  void sendSensorData(const SensorData& data);

  // Các hàm truy vấn trạng thái và thông tin liên kết.
  bool   linked() const { return _state == FN_LINKED; }
  String macMe()  const { return WiFi.macAddress(); }   // MAC IN LÊN WEB/OTA
  String macCyd() const;
  uint8_t channel() const { return _ch; }
  void   unpair();

  // Lưu trạng thái máy, địa chỉ MAC, mốc thời gian và callback ESP-NOW.
private:
  static FarmNow* _self;

  volatile FnState  _state = FN_HOP;
  uint8_t  _cyd[6] = {0};
  uint8_t  _ch = FN_CH_MIN;
  uint32_t _lastHop = 0, _pairSince = 0, _lastReq = 0;
  volatile uint32_t _lastRx = 0;

  // Lệnh bơm từ CYD (task WiFi ghi, loop() đọc). volatile là đủ: 1 int + 1 bool,
  // ghi/đọc  trên ESP32. 
  volatile bool _pumpNew = false;
  volatile int  _pumpVal = -1;

  // Hai hàm hỗ trợ làm tròn và chỉ thêm dữ liệu cảm biến hợp lệ vào JSON.
  float _roundTo(float v, int digits);
  void  _addIfValid(JsonObject o, const char* k, float v, bool ok, int digits);

  // Nhóm hàm nội bộ quản lý kênh, peer, chuyển trạng thái và xử lý gói nhận.
  void _setChannel(uint8_t ch);
  bool _addPeer(const uint8_t* mac);
  void _gotoHop(const char* why);
  void _onRecv(const uint8_t* mac, const uint8_t* data, int len);

  // ESP32 Arduino core 2.x và 3.x có chữ ký callback KHÁC NHAU.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  static void _recvCb(const esp_now_recv_info_t* info, const uint8_t* d, int l);
#else
  static void _recvCb(const uint8_t* mac, const uint8_t* d, int l);
#endif
};

#endif
