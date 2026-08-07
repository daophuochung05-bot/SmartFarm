#include "Config.h"
#include "Relay.h"
#include "I2C.h"
#include "Sensor_Reading.h"
#include "FarmNow.h"      
// ESP32_SENSOR coi như là 1 trạm nuôi trồng
// Và sẽ gửi về chuỗi Json cho CYD chính đóng vai trò màn hình để hiển thị và kiểm soát thông số của bể dung dịch 

I2C i2cBus(I2C_SDA_PIN, I2C_SCL_PIN);

Relay pumpRelay(RELAY_PUMP_PIN, RELAY_ACTIVE_LOW);
Relay tdsRelay(RELAY_TDS_PIN, RELAY_ACTIVE_LOW);

Sensor_Reading sensorReading(&tdsRelay);

FarmNow farm;     //Trước kia dùng UART, nhưng giờ đổi thành ESP_NOW     

// Thời gian để đọc cảm biến, đọc TDS, gửi Json
unsigned long lastSensorReadTime = 0;
unsigned long lastTdsReadTime = 0;
unsigned long lastJsonSendTime = 0;

//Thứ tự khởi tạo: Serial -> relay -> ESP-NOW -> I2C -> cảm biến -> trạng thái bơm.
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== HYDROPONIC MAIN ESP32 START (ESP-NOW) ===");

  // RELAY INIT
  pumpRelay.begin();
  tdsRelay.begin();

  pumpRelay.off();   //Tắt 2 relay 
  tdsRelay.off();

  // ESP-NOW INIT 
  // In MAC của con này ra Serial — cũng là MAC sẽ hiện trên web.
  farm.begin();

  // I2C INIT 
  i2cBus.begin();
  i2cBus.scan();

  // SENSOR INIT 
  sensorReading.begin();

#if START_PUMP_ON
  pumpRelay.on();
  Serial.println("Pump default: ON");
#else
  pumpRelay.off();
  Serial.println("Pump default: OFF");
#endif
}

void loop() {
  unsigned long now = millis();

  // Các trạng thái ESP-NOW: dò kênh / xin ghép / canh mất kết nối.
  // Gọi ở loop(). Bỏ là nằm chết ở kênh 1, ko bao giờ tìm ra CYD.
  farm.update();

  // Nhận lệnh {"pump":1} hoặc {"pump":0} từ CYD và cập nhật relay bơm.
  int pumpCommand = -1;

  if (farm.readPumpCommand(pumpCommand)) {
    if (pumpCommand == 1) {
      pumpRelay.on();
      Serial.println("Command from screen: Pump ON");
    } else if (pumpCommand == 0) {
      pumpRelay.off();
      Serial.println("Command from screen: Pump OFF");
    }
  }

// Đọc các cảm biến cơ bản: nhiệt độ nước, nhiệt độ/độ ẩm ko khí và ánh sáng.
  if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadTime = now;

    sensorReading.readBasicSensors();

    SensorData data = sensorReading.getData();

    //In giá trị cảm biến ra laptop nên chỉ phục vụ test
    /*
    Serial.println();
    Serial.println("=== Basic Sensors ===");

    Serial.print("Air Temp: ");
    Serial.println(data.airValid ? String(data.air, 1) + " C" : "null");

    Serial.print("Humidity: ");
    Serial.println(data.airValid ? String(data.hum, 1) + " %" : "null");

    Serial.print("Water Temp: ");
    Serial.println(data.h2oValid ? String(data.h2o, 1) + " C" : "null");

    Serial.print("Light: ");
    Serial.println(data.luxValid ? String(data.lux, 0) + " lux" : "null");

    Serial.println("=====================");
    */
  }

  /*
  Đọc TDS theo chu kỳ riêng vì phép đo cần bật nguồn đầu dò,
  chờ ổn định và lấy trung bình nhiều mẫu nên có thể chặn khoảng 3 giây.
  */
  /* 2 giây warm-up relay + 20 mẫu × 50 ms. Chỉ đọc khi đã ghép để tránh
    bỏ lỡ beacon trong lúc mạch cảm biến đang dò kênh tìm CYD.
  */
  if (farm.linked() && now - lastTdsReadTime >= TDS_READ_INTERVAL_MS) {
    lastTdsReadTime = now;
    sensorReading.readTdsSensor();
  }

// Lấy bản sao dữ liệu mới nhất, đóng gói JSON và truyền sang CYD qua ESP-NOW.
  if (now - lastJsonSendTime >= JSON_SEND_INTERVAL_MS) {
    lastJsonSendTime = now;

    SensorData data = sensorReading.getData();
    farm.sendSensorData(data);      // chưa ghép -> tự bỏ qua
  }
}
