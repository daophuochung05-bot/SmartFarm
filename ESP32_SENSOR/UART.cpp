#include "UART.h"

// Hàm tạo lưu cổng Serial và khởi tạo bộ đệm nhận rỗng.
UART::UART(HardwareSerial* serialPort)
  : uart(serialPort), rxBuffer("") {}

// Mở UART theo cấu hình baud, RX, TX và khung dữ liệu 8N1.
void UART::begin(unsigned long baud, int rxPin, int txPin) {
  uart->begin(baud, SERIAL_8N1, rxPin, txPin);
}

// Làm tròn số thực theo số chữ số thập phân yêu cầu.
float UART::roundTo(float value, int digits) {
  if (isnan(value)) return NAN;

  float factor = 1.0;

  for (int i = 0; i < digits; i++) {
    factor *= 10.0;
  }

  return round(value * factor) / factor;
}

// Chỉ thêm key vào JSON khi cờ hợp lệ bật và giá trị không phải NAN.
void UART::addValueIfValid(JsonObject obj, const char* key, float value, bool isValid, int digits) {
  if (!isValid) {
    return;
  }

  if (isnan(value)) {
    return;
  }

  obj[key] = roundTo(value, digits);
}

/*
 * Đọc dữ liệu UART theo từng ký tự cho tới khi gặp '\n'.
 * Sau đó giải mã JSON và lấy lệnh pump nếu có.
 */
bool UART::readPumpCommand(int& pumpValue) {
  while (uart->available()) {
    char c = uart->read();

    // Ký tự xuống dòng đánh dấu kết thúc một gói JSON.
    if (c == '\n') {
      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, rxBuffer);

      // Xóa bộ đệm ngay sau khi đã chụp nội dung để chuẩn bị nhận gói kế tiếp.
      rxBuffer = "";

      if (error) {
        Serial.println("UART JSON parse error");
        return false;
      }

      if (doc.containsKey("pump")) {
        pumpValue = doc["pump"].as<int>();
        return true;
      }

      return false;
    }

    // Bỏ ký tự '\r' để tương thích cả định dạng xuống dòng CRLF.
    if (c != '\r') {
      rxBuffer += c;
    }

    // Chặn bộ đệm tăng vô hạn khi phía gửi không truyền ký tự kết thúc dòng.
    if (rxBuffer.length() > 200) {
      rxBuffer = "";
    }
  }

  return false;
}

/*
 * Tạo JSON chứa các cảm biến hợp lệ, gửi qua UART và in bản sao ra Serial Monitor.
 * Các key giống hoàn toàn gói ESP-NOW của FarmNow.
 */
void UART::sendSensorData(const SensorData& data) {
  StaticJsonDocument<256> doc;
  JsonObject obj = doc.to<JsonObject>();

  addValueIfValid(obj, "ph",  data.ph,  data.phValid,  1);
  addValueIfValid(obj, "tds", data.tds, data.tdsValid, 0);
  addValueIfValid(obj, "lux", data.lux, data.luxValid, 0);
  addValueIfValid(obj, "hum", data.hum, data.airValid, 1);
  addValueIfValid(obj, "air", data.air, data.airValid, 1);
  addValueIfValid(obj, "h2o", data.h2o, data.h2oValid, 1);

  serializeJson(doc, *uart);
  uart->println();

  Serial.print("Send to CYD: ");
  serializeJson(doc, Serial);
  Serial.println();
}
