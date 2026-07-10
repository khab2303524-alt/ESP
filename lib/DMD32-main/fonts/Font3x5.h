#ifndef FONT_3X5_DMD_H
#define FONT_3X5_DMD_H

#include <Arduino.h>
#include <DMD32.h>

// Định nghĩa Font: Số (3x5), riêng % và °C là (5x5) lưu trong bộ nhớ PROGMEM
const uint8_t Font3x5_Custom[] PROGMEM = {
    3, 5, // Chiều rộng mặc định chữ số = 3, Chiều cao = 5
    0, 0, // Dự phòng

    // --- SỐ 0 ĐẾN 9 (Mỗi số chiếm đúng 3 byte) ---
    0x1F, 0x11, 0x1F, // 0 (index 0)
    0x12, 0x1F, 0x10, // 1 (index 1)
    0x1D, 0x15, 0x17, // 2 (index 2)
    0x15, 0x15, 0x1F, // 3 (index 3)
    0x07, 0x04, 0x1F, // 4 (index 4)
    0x17, 0x15, 0x1D, // 5 (index 5)
    0x1F, 0x15, 0x1D, // 6 (index 6)
    0x01, 0x01, 0x1F, // 7 (index 7)
    0x1F, 0x15, 0x1F, // 8 (index 8)
    0x17, 0x15, 0x1F, // 9 (index 9)

    // --- KÝ TỰ ĐẶC BIỆT 5x5 (Mỗi ký tự chiếm đúng 5 byte) ---
    0x11, 0x08, 0x04, 0x02, 0x11, // %  (index 10)
    0x01, 0x00, 0x1F, 0x11, 0x11, // °C (index 11)

    // --- CHỮ CÁI 3x5 CHO NHÃN NHIỆT ĐỘ / ĐỘ ẨM (Mỗi chữ chiếm đúng 3 byte) ---
    0x1E, 0x05, 0x1E, // A (index 12)
    0x01, 0x1F, 0x01, // T (index 13)
    0x1F, 0x05, 0x1A, // R (index 14)
    0x1F, 0x04, 0x1F, // H (index 15)

    // --- DẤU HAI CHẤM THU NHỎ (Chiếm đúng 1 byte, rộng 1px) ---
    0x0A // : (index 16) -> Nhị phân 01010 (sáng ở hàng 2 và hàng 4 như ảnh của bạn)
};

/**
 * Hàm vẽ một ký tự custom hỗ trợ thay đổi chiều rộng linh hoạt
 * @param width: 1 cho dấu :, 3 cho số hoặc chữ cái, 5 cho ký tự % hoặc °C
 */
void drawCustomChar3x5(DMD &dmd, int x, int y, int index, int width)
{
    int font_index = 4; // Bỏ qua 4 byte đầu định nghĩa font

    // Tính toán offset chính xác dựa theo index chỉ định
    switch (index)
    {
    case 10:              // Ký tự %
        font_index += 30; // Sau 10 chữ số (10 * 3 byte = 30)
        break;
    case 11:              // Ký tự °C
        font_index += 35; // Sau 10 chữ số (30 byte) + 1 ký tự % (5 byte)
        break;
    case 12:              // Chữ A
        font_index += 40; // Sau 10 chữ số (30 byte) + % (5 byte) + °C (5 byte)
        break;
    case 13:              // Chữ T
        font_index += 43; // Sau chữ A (3 byte)
        break;
    case 14:              // Chữ R
        font_index += 46; // Sau chữ T (3 byte)
        break;
    case 15:              // Chữ H
        font_index += 49; // Sau chữ R (3 byte)
        break;
    case 16:              // Dấu hai chấm :
        font_index += 52; // Sau chữ H (3 byte) -> (49 + 3 = 52)
        break;
    default: // Từ 0 đến 9
        font_index += (index * 3);
        break;
    }

    // Tiến hành quét và vẽ pixel lên màn hình LED
    for (int col = 0; col < width; col++)
    {
        uint8_t col_data = pgm_read_byte(&(Font3x5_Custom[font_index + col]));
        for (int row = 0; row < 5; row++)
        {
            if (col_data & (1 << row))
            {
                dmd.writePixel(x + col, y + row, GRAPHICS_NORMAL, 1);
            }
            else
            {
                dmd.writePixel(x + col, y + row, GRAPHICS_INVERSE, 1);
            }
        }
    }
}

/**
 * Tra cứu index font (3x5) tương ứng với 1 chữ cái nhãn.
 * Hỗ trợ thêm dấu hai chấm ':'
 */
int layIndexChuCai3x5(char c)
{
    if (c == ':')
        return 16; // Trả về index của dấu hai chấm

    switch (toupper(c))
    {
    case 'A':
        return 12;
    case 'T':
        return 13;
    case 'R':
        return 14;
    case 'H':
        return 15;
    default:
        return -1;
    }
}

/**
 * Hàm vẽ nhanh 1 chữ cái nhãn hoặc dấu hai chấm tại vị trí (x, y)
 * Tự động nhận diện độ rộng (Dấu ':' rộng 1px, Chữ cái rộng 3px)
 */
void ve1ChuCai3x5(DMD &dmd, int x, int y, char c)
{
    int idx = layIndexChuCai3x5(c);
    if (idx < 0)
        return; // Ký tự không hỗ trợ -> bỏ qua

    int width = (c == ':') ? 1 : 3; // Nếu là dấu ':' thì rộng 1px, ngược lại rộng 3px
    drawCustomChar3x5(dmd, x, y, idx, width);
}

/**
 * Hàm in chuỗi số kèm ký tự đặc biệt theo font tùy biến
 * @param type: 0 = Chỉ in số, 1 = Thêm đuôi '%', 2 = Thêm đuôi '°C'
 */
void printIn3x5(DMD &dmd, int x, int y, String targetStr, int type)
{
    int current_x = x;

    // Duyệt và in các chữ số
    for (int i = 0; i < targetStr.length(); i++)
    {
        char c = targetStr.charAt(i);
        if (c >= '0' && c <= '9')
        {
            int num_index = c - '0';
            drawCustomChar3x5(dmd, current_x, y, num_index, 3);
            current_x += 4; // Độ rộng 3px + 1px khoảng cách
        }
    }

    // In ký tự đuôi 5x5 đi kèm
    if (type == 1)
    {
        drawCustomChar3x5(dmd, current_x += 1, y, 10, 5);
    }
    else if (type == 2)
    {
        drawCustomChar3x5(dmd, current_x += 1, y, 11, 5);
    }
}

#endif