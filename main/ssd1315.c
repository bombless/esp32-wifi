
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <memory.h>


// SSD1315 配置
#define SSD1315_ADDR 0x3C       // I2C 地址（0x3C 或 0x3D）
#define SCREEN_WIDTH 128        // OLED 宽度（像素）
#define SCREEN_HEIGHT 64        // OLED 高度（像素）

// I2C 引脚配置
#define I2C_MASTER_SCL_IO 22    // SCL 引脚
#define I2C_MASTER_SDA_IO 21    // SDA 引脚
#define I2C_MASTER_FREQ_HZ 400000  // I2C 时钟频率（400kHz）

// SSD1315 命令定义
#define SSD1315_DISPLAY_OFF 0xAE
#define SSD1315_DISPLAY_ON 0xAF
#define SSD1315_SET_DISPLAY_CLOCK_DIV 0xD5
#define SSD1315_SET_MULTIPLEX_RATIO 0xA8
#define SSD1315_SET_DISPLAY_OFFSET 0xD3
#define SSD1315_SET_START_LINE 0x40
#define SSD1315_CHARGE_PUMP 0x8D
#define SSD1315_MEMORY_MODE 0x20
#define SSD1315_SEG_REMAP 0xA1
#define SSD1315_COM_SCAN_DIR 0xC8
#define SSD1315_SET_COMPINS 0xDA
#define SSD1315_SET_CONTRAST 0x81
#define SSD1315_SET_PRECHARGE 0xD9
#define SSD1315_SET_VCOM_DETECT 0xDB
#define SSD1315_DEACTIVATE_SCROLL 0x2E
#define SSD1315_SET_PAGE_ADDR 0x22
#define SSD1315_SET_COL_ADDR 0x21



// 初始化 I2C
void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

// 发送命令到 SSD1315
static void ssd1315_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};  // 0x00 表示命令模式
    i2c_master_write_to_device(I2C_NUM_0, SSD1315_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(1000));
}

// 初始化 SSD1315
static void ssd1315_init() {
    ssd1315_write_cmd(SSD1315_DISPLAY_OFF);
    ssd1315_write_cmd(SSD1315_SET_DISPLAY_CLOCK_DIV);
    ssd1315_write_cmd(0x80);  // 建议值
    ssd1315_write_cmd(SSD1315_SET_MULTIPLEX_RATIO);
    ssd1315_write_cmd(0x3F);  // 128x64: 0x3F
    ssd1315_write_cmd(SSD1315_SET_DISPLAY_OFFSET);
    ssd1315_write_cmd(0x00);  // 无偏移
    ssd1315_write_cmd(SSD1315_SET_START_LINE | 0x00);
    ssd1315_write_cmd(SSD1315_CHARGE_PUMP);
    ssd1315_write_cmd(0x14);  // 启用电荷泵
    ssd1315_write_cmd(SSD1315_MEMORY_MODE);
    ssd1315_write_cmd(0x00);  // 水平寻址模式
    ssd1315_write_cmd(SSD1315_SEG_REMAP | 0x01);  // 列地址 127 映射到 SEG0
    ssd1315_write_cmd(SSD1315_COM_SCAN_DIR);      // 反向扫描
    ssd1315_write_cmd(SSD1315_SET_COMPINS);
    ssd1315_write_cmd(0x12);  // 128x64: 0x12
    ssd1315_write_cmd(SSD1315_SET_CONTRAST);
    ssd1315_write_cmd(0xCF);  // 对比度设置
    ssd1315_write_cmd(SSD1315_SET_PRECHARGE);
    ssd1315_write_cmd(0xF1);  // 预充电周期
    ssd1315_write_cmd(SSD1315_SET_VCOM_DETECT);
    ssd1315_write_cmd(0x40);  // VCOMH 电压
    ssd1315_write_cmd(SSD1315_DEACTIVATE_SCROLL);
    ssd1315_write_cmd(SSD1315_DISPLAY_ON);
}

// 清屏
static void ssd1315_clear() {
    // 设置列地址范围
    ssd1315_write_cmd(SSD1315_SET_COL_ADDR);
    ssd1315_write_cmd(0);     // 起始列
    ssd1315_write_cmd(127);   // 结束列
    
    // 设置页地址范围
    ssd1315_write_cmd(SSD1315_SET_PAGE_ADDR);
    ssd1315_write_cmd(0);     // 起始页
    ssd1315_write_cmd(7);     // 结束页（128x64: 7）

    // 准备数据缓冲区：控制字节 + 数据
    uint8_t data_buf[129];
    data_buf[0] = 0x40;  // 数据控制字节
    memset(&data_buf[1], 0, 128);  // 128字节的0数据
    
    // 发送8页数据
    for (int page = 0; page < 8; page++) {
        i2c_master_write_to_device(I2C_NUM_0, SSD1315_ADDR, data_buf, sizeof(data_buf), pdMS_TO_TICKS(1000));
    }
}
static uint8_t display_buffer[128 * 8]; // 128列 × 8页

// 绘制一个像素点（x: 0-127, y: 0-63）
static void ssd1315_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        ESP_LOGI("SSD1315", "Pixel out of bounds: (%d, %d)", x, y);
        return;
    }

    uint8_t page = y / 8;          // 每个 page 包含 8 行
    uint8_t bit_pos = y % 8;       // bit 位置
    uint8_t bit_mask = 1 << bit_pos;
    
    // 计算在缓冲区中的位置
    uint16_t buffer_index = page * 128 + x;
    
    // 修改缓冲区中的对应位
    if (on) {
        display_buffer[buffer_index] |= bit_mask;   // 设置位
    } else {
        display_buffer[buffer_index] &= ~bit_mask;  // 清除位
    }
    
    // 将修改后的字节写入显示器
    // ssd1315_write_cmd(SSD1315_SET_COL_ADDR);
    // ssd1315_write_cmd(x);
    // ssd1315_write_cmd(x);
    // ssd1315_write_cmd(SSD1315_SET_PAGE_ADDR);
    // ssd1315_write_cmd(page);
    // ssd1315_write_cmd(page);
    
    // uint8_t buf[2] = {0x40, display_buffer[buffer_index]};
    // i2c_master_write_to_device(I2C_NUM_0, SSD1315_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(1000));
}

// 初始化显示缓冲区
void ssd1315_clear_buffer(void) {
    memset(display_buffer, 0, sizeof(display_buffer));
}

// 将整个缓冲区刷新到显示器
void ssd1315_refresh_display(void) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1315_write_cmd(SSD1315_SET_COL_ADDR);
        ssd1315_write_cmd(0);
        ssd1315_write_cmd(127);
        ssd1315_write_cmd(SSD1315_SET_PAGE_ADDR);
        ssd1315_write_cmd(page);
        ssd1315_write_cmd(page);
        
        uint8_t buf[129];
        buf[0] = 0x40; // 数据模式
        memcpy(&buf[1], &display_buffer[page * 128], 128);
        i2c_master_write_to_device(I2C_NUM_0, SSD1315_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(1000));
    }
}


const uint16_t* character_get_bitmap(uint16_t character);
#include "cursor.h"
static void draw_cjk(wchar_t character, struct cursor* cursor) {
    int i_start = character > 127 ? 0 : 4;
    int i_end = character > 127 ? 16 : 12; // CJK字符占用16x16像素，ASCII字符占用8x16像素
    for (int i = i_start ; i < i_end; i += 1) {
        for (int j = 0; j < 16; j += 1) {
            

            int offset = j * 16 + i; // 计算偏移量

            int word_offset = offset / 16; // 每个字节包含8位
            int bit_offset = offset % 16; // 计算位偏移

            uint16_t *bitmap = character_get_bitmap(character);
            if (bitmap == NULL) {
                ESP_LOGE("SSD1315", "Character bitmap not found for: %04X", character);
                return;
            }
            if (bitmap[word_offset] & (1 << bit_offset)) {
                ssd1315_draw_pixel(i + cursor->x - i_start, j + cursor->y, true);
            }
        }
    }
    ssd1315_refresh_display();
}

#include <wchar.h>

struct cursor draw_text(wchar_t *text, struct cursor cursor) {
    for (int i = 0; i < wcslen(text); i += 1) {
        cursor_check(&cursor, SCREEN_WIDTH);
        draw_cjk(text[i], &cursor);
        if (text[i] > 255) cursor_next(&cursor);
        else cursor_next_half(&cursor);
    }
    return cursor;
}
