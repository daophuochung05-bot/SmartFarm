#ifndef _Relay_h
#define _Relay_h

#include <Arduino.h>

class Relay {
private:
  // pin: GPIO điều khiển; activeLow: LOW là bật; stateOn: trạng thái logic hiện tại.
  uint8_t pin;
  bool activeLow;
  bool stateOn;

public:
  // Hàm tạo chỉ lưu cấu hình, chưa tác động lên chân GPIO.
  Relay(uint8_t relayPin, bool isActiveLow = true)
    : pin(relayPin), activeLow(isActiveLow), stateOn(false) {}

  // Khởi tạo GPIO đầu ra và đưa relay về trạng thái tắt.
  void begin() {
    pinMode(pin, OUTPUT);
    off();
  }

  // Bật relay; mức điện áp thực tế phụ thuộc activeLow.
  void on() {
    stateOn = true;
    digitalWrite(pin, activeLow ? LOW : HIGH);
  }

  // Tắt relay và cập nhật biến trạng thái nội bộ.
  void off() {
    stateOn = false;
    digitalWrite(pin, activeLow ? HIGH : LOW);
  }

  // Đặt trạng thái relay bằng một biến bool để gọi thuận tiện từ logic điều khiển.
  void set(bool onState) {
    if (onState) {
      on();
    } else {
      off();
    }
  }

  // Trả về trạng thái logic đã lưu lại để không cần phải đọc ngược điện áp thực tế tại chân.
  bool isOn() const {
    return stateOn;
  }
};

#endif