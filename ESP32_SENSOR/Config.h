#ifndef _Config_h
#define _Config_h
/*
  SDA/SCL là hai đường dữ liệu dùng chung.
  ADS1115, SHT31 và BH1750 được phân biệt bằng địa chỉ riêng trên cùng bus.
 */
#define I2C_SDA_PIN            18
#define I2C_SCL_PIN            19

#define ADS1115_ADDR           0x48
#define SHT31_ADDR             0x44
#define BH1750_ADDR            0x23

#define DS18B20_DATA_PIN       26

/*
  Hai relay dùng mức kích hoạt thấp khi RELAY_ACTIVE_LOW = true:
  GPIO LOW làm relay bật, GPIO HIGH làm relay tắt.
 */
#define RELAY_PUMP_PIN         17
#define RELAY_TDS_PIN          16
#define RELAY_ACTIVE_LOW       true      // nếu để false thì chân GPIO tương ứng sẽ kích HIGH để bật Relay

#define START_PUMP_ON          0         // 1 = bật bơm ngay lúc bắt đầu 

/*
  Đầu dò TDS được cấp nguồn qua relay riêng. Chỉ bật đầu dò khi đo,
  sau đó tắt lại để hạn chế điện phân và ăn mòn điện cực(bảo vệ đầu dò).
 */
#define TDS_ADS_CHANNEL        0         // A0 của ADS1115
#define USE_TDS_POWER_RELAY    true      // nếu để false thì sẽ luôn đọc cảm biến mà bỏ qua bước bật relay thông qua GPIO
#define TDS_WARMUP_MS          2000      // chờ ổn định sau khi cấp điện

//Thời gian đọc cảm biến và gửi Json qua 
#define SENSOR_READ_INTERVAL_MS   2000     // đọc DS18B20 / SHT3x / BH1750
#define TDS_READ_INTERVAL_MS      10000    // TDS đọc mỗi 10 giây; một lần đo  chặn khoảng 3 giây
#define JSON_SEND_INTERVAL_MS     1000     // gửi JSON sang CYD mỗi giây

/*  
  UART — ĐÃ BỎ File ở bên trên có thể xóa vì không sử dụng nữa 
  Đường truyền sang CYD giờ là ESP-NOW (FarmNow.h)
*/

#endif
