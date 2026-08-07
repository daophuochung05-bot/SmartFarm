#ifndef _UART_h
#define _UART_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Sensor_Reading.h"

// Lớp UART đóng gói việc nhận lệnh bơm và gửi dữ liệu cảm biến qua HardwareSerial.
class UART {
private:
  // Con trỏ tới cổng UART và bộ đệm tích lũy dữ liệu nhận theo từng ký tự.
  HardwareSerial* uart;
  String rxBuffer;

  // Hàm hỗ trợ làm tròn và thêm field hợp lệ vào gói JSON.
  float roundTo(float value, int digits);

  // Chỉ thêm field vào JSON nếu dữ liệu hợp lệ
  void addValueIfValid(JsonObject obj, const char* key, float value, bool isValid, int digits);

public:
  // Hàm tạo nhận địa chỉ cổng Serial phần cứng, ví dụ &Serial2.
  UART(HardwareSerial* serialPort);

  // Khởi tạo baud rate, chân RX/TX và định dạng 8N1.
  void begin(unsigned long baud, int rxPin, int txPin);

  // Đọc một dòng JSON; trả true khi nhận được key pump hợp lệ.
  bool readPumpCommand(int& pumpValue);
  // Đóng gói các cảm biến hợp lệ và gửi một dòng JSON sang CYD.
  void sendSensorData(const SensorData& data);
};

#endif