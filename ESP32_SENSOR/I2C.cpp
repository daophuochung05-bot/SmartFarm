#include "I2C.h"

// Hàm tạo: nhận chân SDA/SCL và lưu thuộc tính của đối tượng.
I2C::I2C(uint8_t sda, uint8_t scl)
  : sdaPin(sda), sclPin(scl) {}

// Bắt đầu bộ điều khiển I2C của ESP32 với chân SDA/SCL tùy chọn.
void I2C::begin() {
  Wire.begin(sdaPin, sclPin);
}

/*
  Quét toàn bộ không gian địa chỉ I2C 7-bit.
  Thiết bị phản hồi mã lỗi 0 nghĩa là đã trả ACK và đang có mặt trên bus.
 */
void I2C::scan() {
  //In ra màn hình để test có thể uncomment nếu muốn 
  /*
  Serial.println();
  Serial.println("Scanning I2C devices...");
  */
  byte count = 0;

  // Địa chỉ 0 và 127 được dành riêng, nên chỉ kiểm tra từ 1 đến 126.
  for (byte address = 1; address < 127; address++) {
    // Gửi điều kiện START tới địa chỉ đang kiểm tra.
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    // error == 0: thiết bị đã phản hồi ACK.
    if (error == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      count++;
    }
  }

  // Báo tổng số thiết bị tìm thấy để hỗ trợ kiểm tra đấu nối.
  if (count == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Total I2C devices: ");
    Serial.println(count);
  }

  Serial.println();
}