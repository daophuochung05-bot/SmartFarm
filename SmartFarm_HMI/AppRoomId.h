#pragma once

#include <Arduino.h>
#include <mbedtls/sha256.h>

// Chuỗi muối bí mật, phải trùng khớp hoàn toàn với FARM_ROOM_SALT bên file web smartfarm.html
// Nếu lệch ký tự thì Web và ESP32 sẽ không tìm thấy nhau
#define ROOM_SALT "MacomVn-Y2K-LayerMakerFarm-DOI-CHUOI-NAY-2026"

// Hàm băm Chat ID kết hợp với chuỗi muối để tạo mã phòng bảo mật cho MQTT Topic
inline String deriveRoomId(const String& chatId) {
    // Nếu chưa nhập Chat ID thì trả về unset để vào phòng thử nghiệm dùng chung
    if (chatId.length() == 0) return "unset";

    // Ghép chuỗi muối với Chat ID làm đầu vào cho thuật toán băm
    String input = String(ROOM_SALT) + ":" + chatId;

    // Khởi tạo và thực hiện thuật toán băm SHA256 để lấy kết quả 32 byte dữ liệu thô
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);          // Tham số 0 cấu hình chế độ SHA256 thay vì SHA224
    mbedtls_sha256_update(&ctx, (const unsigned char*)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    // Chuyển đổi 20 byte đầu tiên thành chuỗi ký tự Hex dài 40 ký tự để làm mã định danh phòng gọn nhẹ
    static const char* HEXCH = "0123456789abcdef";
    String room;
    room.reserve(40);
    for (int i = 0; i < 20; i++) {
        room += HEXCH[(hash[i] >> 4) & 0xF];     // Trích xuất lấy 4 bit cao của byte
        room += HEXCH[hash[i] & 0xF];            // Trích xuất lấy 4 bit thấp của byte
    }
    return room;
}