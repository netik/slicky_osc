#include "hidapi.h"
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#define MAX_STR 255
#define VENDOR_ID 0x04D8
#define PRODUCT_ID 0xEC24

#define true 1
#define false 0
#define nullptr (NULL *)
#define uint8_t unsigned char
#define bool int

hid_device *hidDevice;

bool isConnected() {
    if (hidDevice != NULL) hid_close(hidDevice);
    hidDevice = hid_open(VENDOR_ID, PRODUCT_ID, NULL);

    if (hidDevice == NULL) {
      return false;
    }
    return true;
}

int writeBuffer(unsigned char buf[65]) {
    int res = -1;

    if (isConnected()) {
      res = hid_write(hidDevice, buf, 65); // 65 on success, -1 on failure
      if (res < 0) printf("Error: Problem writing to hid device.");
    }
    else 
      printf("Error: No device connected.");

    return res;
}

int setColor(char r, char g, char b, char w) {
    unsigned char buf[65]; // 65 bytes
    // The first byte is the write endpoint (0x00).
    // 0A 04 00 00 WW BB GG RR
    buf[0] = 0x00;
    buf[1] = 0x0A;
    buf[2] = 0x04;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = w;
    buf[6] = b;
    buf[7] = g;
    buf[8] = r;

    return writeBuffer(buf);
}

typedef uint32_t color_rgb_t;

color_rgb_t util_hsv_to_rgb(float H, float S, float V) {
  H = fmodf(H, 1.0);

  float h = H * 6;
  uint8_t i = floor(h);
  float a = V * (1 - S);
  float b = V * (1 - S * (h - i));
  float c = V * (1 - (S * (1 - (h - i))));
  float rf, gf, bf;

  switch (i) {
    case 0:
      rf = V * 255;
      gf = c * 255;
      bf = a * 255;
      break;
    case 1:
      rf = b * 255;
      gf = V * 255;
      bf = a * 255;
      break;
    case 2:
      rf = a * 255;
      gf = V * 255;
      bf = c * 255;
      break;
    case 3:
      rf = a * 255;
      gf = b * 255;
      bf = V * 255;
      break;
    case 4:
      rf = c * 255;
      gf = a * 255;
      bf = V * 255;
      break;
    case 5:
    default:
      rf = V * 255;
      gf = a * 255;
      bf = b * 255;
      break;
  }

  uint8_t R = rf;
  uint8_t G = gf;
  uint8_t B = bf;

  color_rgb_t RGB = (R << 16) + (G << 8) + B;
  return RGB;
}


void led_set_rgb(color_rgb_t rgb) {
  setColor((rgb >> 16) & 0xFF, 
           (rgb >> 8) & 0xFF,
            rgb & 0xFF, 0);
}

void led_pattern_rainbow(float* p_hue, uint8_t repeat) {
  float hue_step = 1;

  color_rgb_t rgb = util_hsv_to_rgb(*p_hue + (hue_step), 1, 1);
  led_set_rgb(rgb);

  *p_hue += 0.05;
  if (*p_hue > 1) {
    *p_hue -= 1;
  }
}

int main() {
    int res = hid_init();
    float p_hue = 0;

    if (res < 0) {
        printf("Error: Problem initializing the hidapi library.");
    }

    // open device
    hidDevice = hid_open(VENDOR_ID, PRODUCT_ID, NULL);

//    setColor(0x3f, 0xb9, 0xbe, 0);
//     setColor(0xff, 0xff, 0xff, 0);
//    setColor(0xff, 0x36, 0x63, 0xff);

    
    while(1) {
        led_pattern_rainbow(&p_hue, 1);
        usleep(33333);
    }
}
