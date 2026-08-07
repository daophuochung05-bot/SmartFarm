#ifndef _Sensor_Reading_h
#define _Sensor_Reading_h

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_SHT31.h>
#include <BH1750.h>

#include "Config.h"
#include "Relay.h"

/*
  SensorData là gói dữ liệu dùng chung giữa khối đọc cảm biến và khối truyền thông.
  Mỗi giá trị đi kèm cờ Valid; dữ liệu lỗi sẽ ko được gửi sang CYD.
  airValid được dùng chung cho cả nhiệt độ và độ ẩm vì hai giá trị cùng đến từ SHT31.
 */
struct SensorData {
  float ph;
  float tds;
  float lux;
  float hum;
  float air;
  float h2o;

  bool phValid;
  bool tdsValid;
  bool luxValid;
  bool airValid;
  bool h2oValid;
};
//pH đã được dự trù trong gói dữ liệu nhưng chưa có hàm đọc, nên phValid = false.

// Lớp Sensor_Reading khởi tạo cảm biến, đọc dữ liệu và giữ bản đo mới nhất.
class Sensor_Reading {
private:
  // Đối tượng giao tiếp 1-Wire và thư viện DS18B20.
  OneWire oneWire;
  DallasTemperature ds18b20;

  // Ba đối tượng cảm biến giao tiếp qua bus I2C.
  Adafruit_ADS1115 ads;
  Adafruit_SHT31 sht31;
  BH1750 lightMeter;

  // Con trỏ tới relay cấp nguồn đầu dò TDS.
  Relay* tdsRelay;

  // Bản dữ liệu mới nhất của hệ thống.
  SensorData data;

  // Các cờ cho biết từng thiết bị I2C đã khởi tạo thành công hay chưa.
  bool adsReady;
  bool shtReady;
  bool bhReady;

  // Hàm lấy trung bình điện áp TDS và chuyển điện áp thành ppm.
  float readTdsVoltageAverage(int samples);
  float calculateTds(float voltage, float waterTempC);

public:
  // Hàm tạo nhận địa chỉ relay TDS để bật/tắt đầu dò trong quá trình đo.
  Sensor_Reading(Relay* relayTds);

  // Khởi tạo các cảm biến và lưu trạng thái sẵn sàng.
  void begin();

  // Đọc nhóm cảm biến cơ bản, ko có đọc con TDS bằng hàm này.
  void readBasicSensors();
  // Thực hiện toàn bộ chu trình cấp nguồn, lấy mẫu và tính TDS.
  void readTdsSensor();

  // Trả về một bản sao của dữ liệu mới nhất.
  SensorData getData() const;
};

#endif