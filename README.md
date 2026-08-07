# 🌾 Smart Farm IoT System

Hệ thống giám sát và điều khiển nông nghiệp thông minh (Smart Farm) ứng dụng công nghệ IoT. Dự án giúp thu thập dữ liệu môi trường từ các cảm biến, hiển thị trực quan qua màn hình HMI, đồng bộ dữ liệu lên đám mây (Firebase) và cho phép người dùng giám sát, điều khiển thiết bị từ xa thông qua giao diện Web Dashboard và Telegram.

## 📑 Mục lục
- [Cấu trúc dự án](#cấu-trúc-dự-án)
- [Tính năng chính](#tính-năng-chính)
- [Phần cứng sử dụng](#phần-cứng-sử-dụng)
- [Công nghệ & Thư viện](#công-nghệ--thư-viện)
- [Thành viên & Phân công công việc](#thành-viên--phân-công-công-việc)

---

## 📁 Cấu trúc dự án

Dự án được chia thành 3 phần chính tương ứng với các thư mục và tập tin:

1. **`ESP32_SENSOR/`** 
   - Chịu trách nhiệm giao tiếp với các cảm biến (Nhiệt độ, độ ẩm, ánh sáng, pH, TDS...).
   - Đọc và xử lý tín hiệu cảm biến thông qua I2C, OneWire.
   - Truyền dữ liệu sang bộ phận điều khiển trung tâm (HMI) qua giao tiếp UART.

2. **`SmartFarm_HMI/`**
   - Vi điều khiển trung tâm kiêm màn hình hiển thị HMI.
   - Nhận dữ liệu từ bộ đọc cảm biến.
   - Kết nối WiFi, đồng bộ dữ liệu với **Firebase Realtime Database**.
   - Gửi cảnh báo/thông báo qua **Telegram Bot**.
   - Quản lý giao diện hiển thị đồ họa (UI/Theme) và lưu trữ trạng thái.

3. **`smartfarm.html`**
   - Giao diện người dùng (Web Dashboard).
   - Hiển thị thông số môi trường theo thời gian thực (Real-time).
   - Nút điều khiển các cơ cấu chấp hành (bơm nước, đèn chiếu sáng, quạt...).

---

## 🚀 Tính năng chính

- **Đo lường thông số môi trường:** Nhiệt độ & độ ẩm không khí, cường độ ánh sáng, nhiệt độ nước, độ pH, nồng độ TDS.
- **Giám sát thời gian thực:** Cập nhật liên tục trạng thái lên màn hình HMI và giao diện Web.
- **Điều khiển từ xa:** Bật/tắt các thiết bị (Relay) như bơm, quạt, đèn... trực tiếp từ Web.
- **Cảnh báo thông minh:** Tự động gửi tin nhắn báo động qua Telegram khi thông số vượt ngưỡng an toàn.
- **Lưu trữ đám mây:** Sử dụng Firebase để lưu trữ dữ liệu, đảm bảo có thể truy cập từ bất cứ đâu có kết nối Internet.

---

## 🛠 Phần cứng sử dụng

- **Vi điều khiển:** ESP32 (x2 - Một cho cảm biến, một cho HMI).
- **Màn hình:** Màn hình cảm ứng (CYD - Cheap Yellow Display / TFT LCD).
- **Cảm biến môi trường:**
  - `SHT31`: Đo nhiệt độ và độ ẩm không khí (I2C).
  - `BH1750`: Đo cường độ ánh sáng (I2C).
- **Cảm biến thủy canh/đất:**
  - `DS18B20`: Đo nhiệt độ dung dịch/nước (OneWire).
  - Cảm biến pH.
  - Cảm biến TDS.
- **Module khác:** 
  - `ADS1115`: Bộ chuyển đổi ADC 16-bit (I2C) để đọc tín hiệu analog từ cảm biến pH và TDS với độ chính xác cao.
  - Module Relay để điều khiển thiết bị điện (bơm, đèn).

---

## 💻 Công nghệ & Thư viện

- **Ngôn ngữ:** C/C++ (Arduino), HTML, CSS, JavaScript.
- **Flatform:** Arduino IDE / PlatformIO.
- **Dịch vụ Cloud:** Firebase Realtime Database.
- **Thư viện Arduino tiêu biểu:**
  - `Wire.h`, `OneWire.h`, `DallasTemperature.h`
  - `Adafruit_ADS1X15.h`, `Adafruit_SHT31.h`, `BH1750.h`
  - Thư viện kết nối Firebase, Telegram, WiFi.

---

## 👥 Thành viên & Phân công công việc (Contributors)

Dự án được hợp tác phát triển bởi:

* **[Tên của bạn]** ([@username_github_cua_ban](https://github.com/))
  * Xây dựng khối đọc cảm biến `ESP32_SENSOR`.
  * Lập trình giao diện Web Dashboard `smartfarm.html`.
  * Thiết kế giao diện HMI.

* **[Tên bạn cùng nhóm]** ([@username_github_cua_ban_kia](https://github.com/))
  * Lập trình khối `SmartFarm_HMI`.
  * Tích hợp Firebase và Telegram Bot.
  * Thiết kế và đấu nối phần cứng, thử nghiệm hệ thống.

*(Lưu ý: Bạn có thể chỉnh sửa lại phần phân công công việc này cho đúng với thực tế nhóm của bạn)*

> **📝 Ghi chú phát triển:** Dự án ban đầu được phát triển theo hình thức phân chia module độc lập và trao đổi file trực tiếp. Thư mục mã nguồn này được tổng hợp lại và đưa lên GitHub nhằm mục đích lưu trữ, quản lý phiên bản thống nhất và làm tài liệu tham khảo/portfolio.
