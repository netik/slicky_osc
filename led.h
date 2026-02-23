#ifndef LED_H
#define LED_H

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t color_rgb_t;

void led_init(bool test_mode);
void led_set_rgb(color_rgb_t rgb);
void led_pattern_rainbow(float *p_hue, uint8_t repeat);

#endif /* LED_H */
