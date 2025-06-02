
#if !defined _HEADER_CURSOR_
#define _HEADER_CURSOR_
#include "freertos/FreeRTOS.h"
struct cursor {
    uint8_t x;
    uint8_t y;
};

struct cursor cursor_create();

void cursor_next(struct cursor* c);

void cursor_next_half(struct cursor* c);

void cursor_new_line(struct cursor* c);

void cursor_check(struct cursor* c, int screen_width);
#endif