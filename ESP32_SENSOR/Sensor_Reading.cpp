#include "Sensor_Reading.h"

/*
 Hàm tạo liên kết DS18B20 với chân 1-Wire, nhận relay TDS
  và đặt toàn bộ cảm biến ở trạng thái chưa sẵn sàng.
 */
Sensor_Reading::Sensor_Reading(Relay* relayTds)
  : oneWire(DS18B20_DATA_PIN),
    ds18b20(&oneWire),
    tdsRelay(relayTds),
    adsReady(false),
    shtReady(false),
    bhReady(false) {

  // Khởi tạo giá trị bằng NAN để không vô tình sử dụng số chưa được đo.
  data.tds = NAN;
  data.lux = NAN;
  data.hum = NAN;
  data.air = NAN;
  data.h2o = NAN;
  data.ph = NAN;

  // Ban đầu mọi cờ hợp lệ đều false; pH vẫn false vì chưa có cảm biến pH.
  data.tdsValid = false;
  data.luxValid = false;
  data.airValid = false;
  data.h2oValid = false;
  data.phValid = false;
}

/*
  begin() khởi tạo lần lượt DS18B20, ADS1115, SHT31 và BH1750.
 */
void Sensor_Reading::begin() {
  // Đặt DS18B20 ở độ phân giải 12 bit để có độ phân giải nhiệt độ cao nhất.
  ds18b20.begin();
  ds18b20.setResolution(12);
  Serial.println("DS18B20 started");

  // ADS1115 dùng địa chỉ cấu hình và bus Wire đã được I2C::begin() khởi động.
  adsReady = ads.begin(ADS1115_ADDR, &Wire);

  if (adsReady) {
    // GAIN_ONE chọn thang đo ±4.096 V, phù hợp tín hiệu đầu vào khoảng 0..3.3 V.
    ads.setGain(GAIN_ONE);
    Serial.println("ADS1115 OK");
  } else {
    Serial.println("ADS1115 ERROR / Not connected");
  }

  // SHT31 cung cấp đồng thời nhiệt độ và độ ẩm không khí.
  shtReady = sht31.begin(SHT31_ADDR);
  if (shtReady) {
    Serial.println("SHT3x OK");
  } else {
    Serial.println("SHT3x ERROR / Not connected");
  }

  // BH1750 chạy chế độ đo liên tục, độ phân giải cao.
  bhReady = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDR, &Wire);

  if (bhReady) {
    Serial.println("BH1750 OK");
  } else {
    Serial.println("BH1750 ERROR / Not connected");
  }
}

 //Chức năng: cập nhật giá trị của các cảm biến cơ bản.

void Sensor_Reading::readBasicSensors() {
  //Đọc cảm biến 1 dây DS18B20
  ds18b20.requestTemperatures();
  float waterTemp = ds18b20.getTempCByIndex(0);

  // Loại các giá trị không hợp lí
  if (waterTemp != DEVICE_DISCONNECTED_C && waterTemp > -50 && waterTemp < 125) {
    data.h2o = waterTemp;
    data.h2oValid = true;
  } else {
    data.h2o = NAN;
    data.h2oValid = false;
  }

  // Chỉ đọc SHT31 khi thiết bị đã khởi tạo thành công.
  if (shtReady) {
    float airTemp = sht31.readTemperature();
    float hum = sht31.readHumidity();

    // Cả nhiệt độ và độ ẩm phải hợp lệ, nếu một giá trị lỗi thì bỏ cả cặp.
    if (!isnan(airTemp) && !isnan(hum)) {
      data.air = airTemp;
      data.hum = hum;
      data.airValid = true;
    } else {
      data.air = NAN;
      data.hum = NAN;
      data.airValid = false;
    }
  } else {
    data.air = NAN;
    data.hum = NAN;
    data.airValid = false;
  }

  // BH1750 hợp lệ khi trả về số hữu hạn và không âm.
  if (bhReady) {
    float lux = lightMeter.readLightLevel();

    if (!isnan(lux) && lux >= 0) {
      data.lux = lux;
      data.luxValid = true;
    } else {
      data.lux = NAN;
      data.luxValid = false;
    }
  } else {
    data.lux = NAN;
    data.luxValid = false;
  }
}

/*
  Lấy nhiều mẫu ADC từ ADS1115, đổi từng mẫu sang volt,
  loại mẫu ngoài 0..3.3 V rồi trả về giá trị điện áp trung bình.
 */
float Sensor_Reading::readTdsVoltageAverage(int samples) {
  // Ko có ADS1115 thì không thể đo TDS.
  if (!adsReady) return NAN;

  float sumVoltage = 0;
  int validCount = 0;

  // Lấy samples mẫu, với 20 mẫu và delay 50 ms, tốn cỡ 1s.
  for (int i = 0; i < samples; i++) {
    int16_t raw = ads.readADC_SingleEnded(TDS_ADS_CHANNEL);
    float voltage = ads.computeVolts(raw);

    // Chỉ cộng các mẫu nằm trong dải điện áp đầu vào dự kiến.
    if (voltage >= 0.0 && voltage <= 3.3) {
      sumVoltage += voltage;
      validCount++;
    }

    delay(50);
  }

  // Không có mẫu hợp lệ thì trả NAN để tầng trên đánh dấu lỗi.
  if (validCount == 0) return NAN;

  return sumVoltage / validCount;
}

/*
  Chuyển điện áp đầu dò thành TDS (ppm).
  Điện áp được bù theo nhiệt độ nước rồi đưa qua phương trình đa thức thực nghiệm.
 */
float Sensor_Reading::calculateTds(float voltage, float waterTempC) {
  if (isnan(voltage)) return NAN;

  float tempForCompensation = waterTempC;

  // Khi chưa có nhiệt độ nước, dùng 25 độ C làm giá trị bù mặc định.
  if (isnan(tempForCompensation)) {
    tempForCompensation = 25.0;
  }

  // Hệ số bù nhiệt 2% cho mỗi °C lệch khỏi 25 °C.
  float compensationCoefficient = 1.0 + 0.02 * (tempForCompensation - 25.0);
  float compensationVoltage = voltage / compensationCoefficient;

  // Phương trình bậc ba chuyển điện áp đã bù thành giá trị TDS.
  float tdsValue =
    (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
    - 255.86 * compensationVoltage * compensationVoltage
    + 857.39 * compensationVoltage) * 0.5;

  //Kẹp giá trị lại.
  if (tdsValue < 0) {
    tdsValue = 0;
  }

  return tdsValue;
}

/*
   Đọc cảm biến TDS theo các bước(nếu sử dụng relay để cấp nguồn cho TDS sensor):
  1. Bật relay cấp nguồn đầu dò.
  2. Chờ đầu dò ổn định.
  3. Lấy trung bình 20 mẫu điện áp.
  4. Bù nhiệt độ và tính ppm.
  5. Tắt relay để bảo vệ đầu dò.
 */
void Sensor_Reading::readTdsSensor() {
  Serial.println();
  Serial.println("=== Reading TDS ===");

  //Nếu sử dụng relay thì bật relay trước
  if (USE_TDS_POWER_RELAY) {
    tdsRelay->on();
    Serial.println("TDS Relay: ON");
    delay(TDS_WARMUP_MS);
  }

  // Nếu nhiệt độ nước chưa hợp lệ, phép bù TDS dùng mặc định 25 °C.
  float voltage = readTdsVoltageAverage(20);
  float waterTemp = data.h2oValid ? data.h2o : 25.0;
  float tds = calculateTds(voltage, waterTemp);

  // Chỉ cập nhật dữ liệu khi phép tính trả về giá trị hữu hạn.
  if (!isnan(tds)) {
    data.tds = tds;
    data.tdsValid = true;

    // Xóa cái comment nếu gắm dây lap vào để test 
    /*Serial.print("TDS Voltage A0: ");
    Serial.print(voltage, 4);
    Serial.println(" V");

    Serial.print("Water Temp Compensation: ");
    Serial.print(waterTemp, 2);
    Serial.println(" C");

    Serial.print("TDS Value: ");
    Serial.print(data.tds, 2);
    Serial.println(" ppm");*/
  } else {
    data.tds = NAN;
    data.tdsValid = false;
    Serial.println("TDS ERROR");
  }

  // Đọc xong thì tắt relay.
  if (USE_TDS_POWER_RELAY) {
    tdsRelay->off();
    Serial.println("TDS Relay: OFF");
  }

  Serial.println("===================");
  Serial.println();
}

// Trả về bản sao SensorData để chương trình có thể đọc giá trị.
SensorData Sensor_Reading::getData() const {
  return data;
}