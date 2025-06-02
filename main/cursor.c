#include "freertos/FreeRTOS.h"
#if !defined _HEADER_CURSOR_
#define _HEADER_CURSOR_

struct cursor {
    uint8_t x;
    uint8_t y;
};

struct cursor cursor_create() {
    struct cursor c;
    c.x = 0;
    c.y = 0;
    return c;
}

void cursor_next(struct cursor* c) {
    c->x += 16;
}

void cursor_next_half(struct cursor* c) {
    c->x += 8;
}

void cursor_new_line(struct cursor* c) {
    c->x = 0;
    c->y += 16;
}

void cursor_check(struct cursor* c, int screen_width) {
    if (c->x >= screen_width) {
        c->x = 0;
        c->y += 16;
    }
}
#endif