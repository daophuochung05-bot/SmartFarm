#ifndef _I2C_h
#define _I2C_h

#include <Arduino.h>
#include <Wire.h>

// I2C đóng gói các thao tác khởi tạo và kiểm tra bus I2C.
class I2C {
private:
  // Hai chân vật lý được truyền từ Config.h khi tạo đối tượng.
  uint8_t sdaPin;
  uint8_t sclPin;
public:
  // Hàm tạo lưu lại chân SDA và SCL, chưa khởi động phần cứng.
  I2C(uint8_t sda, uint8_t scl);

  // Khởi động bus I2C bằng các chân đã cấu hình.
  void begin();
  // Quét địa chỉ 1..126 và in các thiết bị phản hồi ACK ra Serial.
  void scan();
};
#endif