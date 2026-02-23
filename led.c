#include "led.h"
#include "config.h"
#include "log.h"
#include "hidapi.h"
#include <math.h>
#include <stdint.h>

static hid_device *s_dev;

static bool is_connected(void) {
    if (s_dev != NULL) {
        hid_close(s_dev);
    }
    s_dev = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    return (s_dev != NULL);
}

static int write_buffer(unsigned char buf[65]) {
    int res = -1;
    /* test_mode is handled in led_init: we never call hid_write in test mode */
    if (is_connected()) {
        res = hid_write(s_dev, buf, 65);
        if (res < 0) {
            log_error("Error: Problem writing to hid device.");
        }
    } else {
        log_info("HID error: No device connected.");
    }
    return res;
}

static int set_color(unsigned char r, unsigned char g, unsigned char b, unsigned char w) {
    unsigned char buf[65];
    buf[0] = 0x00;
    buf[1] = 0x0A;
    buf[2] = 0x04;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = w;
    buf[6] = b;
    buf[7] = g;
    buf[8] = r;
    return write_buffer(buf);
}

static color_rgb_t hsv_to_rgb(float H, float S, float V) {
    H = fmodf(H, 1.0f);

    float h = H * 6;
    unsigned char i = (unsigned char)floorf(h);
    float a = V * (1 - S);
    float b = V * (1 - S * (h - i));
    float c = V * (1 - (S * (1 - (h - i))));
    float rf, gf, bf;

    switch (i) {
        case 0:
            rf = V * 255; gf = c * 255; bf = a * 255;
            break;
        case 1:
            rf = b * 255; gf = V * 255; bf = a * 255;
            break;
        case 2:
            rf = a * 255; gf = V * 255; bf = c * 255;
            break;
        case 3:
            rf = a * 255; gf = b * 255; bf = V * 255;
            break;
        case 4:
            rf = c * 255; gf = a * 255; bf = V * 255;
            break;
        case 5:
        default:
            rf = V * 255; gf = a * 255; bf = b * 255;
            break;
    }

    unsigned char R = (unsigned char)rf;
    unsigned char G = (unsigned char)gf;
    unsigned char B = (unsigned char)bf;
    return (color_rgb_t)((R << 16) | (G << 8) | B);
}

void led_init(bool test_mode) {
    if (test_mode) {
        s_dev = NULL;
        return;
    }
    if (hid_init() < 0) {
        log_info("Error: Problem initializing the hidapi library.");
    }
    s_dev = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
}

void led_set_rgb(color_rgb_t rgb) {
    /* In test mode, hid_device is NULL and is_connected() will fail; skip actual write */
    if (s_dev == NULL) {
        return;
    }
    set_color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0);
}

void led_pattern_rainbow(float *p_hue, uint8_t repeat) {
    (void)repeat;
    color_rgb_t rgb = hsv_to_rgb(*p_hue + 1.0f, 1.0f, 1.0f);
    led_set_rgb(rgb);
    *p_hue += 0.05f;
    if (*p_hue > 1.0f) {
        *p_hue -= 1.0f;
    }
}
