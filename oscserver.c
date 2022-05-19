#include "hidapi.h"
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h> 
#include "log.h"
#include "tinyosc.h"

#define DEBUG
#define MAX_STR 255
#define VENDOR_ID 0x04D8
#define PRODUCT_ID 0xEC24

#define nullptr (NULL *)
#define uint8_t unsigned char

static volatile bool keepRunning = true;

// handle Ctrl+C
static void sigintHandler(int x) {
  keepRunning = false;
}

hid_device *hidDevice;

// remember the color, which we'll update once every second.
int currentColor = 0x000000;
int blinks_to_do = 0; // if positive, we'll blink and decrement this.
bool blink_on_change = true;
bool blinking = false;
bool lastState = true;

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
      if (res < 0) log_error("Error: Problem writing to hid device.");
    }
    else 
      log_info("Error: No device connected.");

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

void process_osc_msg(tosc_message osc, int len) {
  char cmd[MAX_STR];

#ifdef DEBUG
  log_debug("Received OSC message: [%i bytes] %s %s",
            len, // the number of bytes in the OSC message
            tosc_getAddress(&osc), // the OSC address string, e.g. "/button1"
            tosc_getFormat(&osc)); // the OSC format string, e.g. "f"
#endif  
  strncpy(cmd, tosc_getAddress(&osc), MAX_STR);

  log_info("cmd: %s", cmd);
  if (strncmp(cmd, "/setcolorint", MAX_STR) == 0) {
    /* we expect a 32-bit rgb color in hex which we will send as a 32-bit int */
    int newcolor = tosc_getNextInt32(&osc);
    currentColor = newcolor;
    blinking = false;
    led_set_rgb((color_rgb_t) newcolor);
    if (blink_on_change) {
      blinks_to_do=6;
    }
  }

  if (strncmp(cmd, "/setcolorhex", MAX_STR) == 0) {
    /* we expect a string to convert */
    const char *hexstr = tosc_getNextString(&osc);

    if (hexstr != NULL) {
      int newcolor = strtol(hexstr, NULL, 16);
      currentColor = newcolor;
      blinking = false;
      led_set_rgb((color_rgb_t) newcolor);
    }

    if (blink_on_change) {
      blinks_to_do=6;
    }
  }

  if (strncmp(cmd, "/blink", MAX_STR) == 0) {
    /* any value > 0 enables blinking */
    int blinkparam = tosc_getNextInt32(&osc);
    if (blinkparam > 0) {
      log_info("set blink on");
      blinking = true;
      // is the color set to anything?
      if (currentColor == 0) {
        currentColor = 0xFF0000;
      }
    } else {
      log_info("set blink off");
      blinking = false;
      led_set_rgb((color_rgb_t) currentColor);
    }
  }

  if (strncmp(cmd, "/blink_on_change", MAX_STR) == 0) {
    /* any value > 0 enables blinking */
    int blinkparam = tosc_getNextInt32(&osc);
    if (blinkparam > 0) {
      log_info("set blink_on_change on");
      blink_on_change = true;
    } else {
      log_info("set blink_on_change off");
      blink_on_change = false;
    }
  }

}

void handleBlink() { 

  if (blinking || blinks_to_do > 0) {  
    if (lastState) {
      led_set_rgb((color_rgb_t) currentColor);
    } else {
      led_set_rgb((color_rgb_t) 0x000000);
    }
    lastState = !lastState;

    if (blinks_to_do > 0) {
      blinks_to_do--;
    }
  }

}

int main() {
  int res = hid_init();
  float p_hue = 0;
  char buffer[2048]; // declare a 2Kb buffer to read packet data into

  if (res < 0) {
    log_info("Error: Problem initializing the hidapi library.");
  }

  // open device
  hidDevice = hid_open(VENDOR_ID, PRODUCT_ID, NULL);

  // register the SIGINT handler (Ctrl+C)
  signal(SIGINT, &sigintHandler);

  // open a socket to listen for datagrams (i.e. UDP packets) on port 9000
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  fcntl(fd, F_SETFL, O_NONBLOCK); // set the socket to non-blocking
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(9000);
  sin.sin_addr.s_addr = INADDR_ANY;
  bind(fd, (struct sockaddr *) &sin, sizeof(struct sockaddr_in));
  log_info("tinyosc is now listening on port 9000.");
  log_info("Press Ctrl+C to stop.");

  while (keepRunning) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(fd, &readSet);
    struct timeval timeout = {1, 0}; // select times out after 1 second
    
    if (select(fd+1, &readSet, NULL, NULL, &timeout) > 0) {
      // a packet is available to read
      struct sockaddr sa; // can be safely cast to sockaddr_in
      socklen_t sa_len = sizeof(struct sockaddr_in);
      int len = 0;

      while ((len = (int) recvfrom(fd, buffer, sizeof(buffer), 0, &sa, &sa_len)) > 0) {
        if (tosc_isBundle(buffer)) {
          tosc_bundle bundle;
          tosc_parseBundle(&bundle, buffer, len);
          const uint64_t timetag = tosc_getTimetag(&bundle);
          tosc_message osc;

          while (tosc_getNextMessage(&bundle, &osc)) {
            // tosc_printMessage(&osc);
            process_osc_msg(osc, len);
          }
        } else {
          tosc_message osc;
          tosc_parseMessage(&osc, buffer, len);
          // tosc_printMessage(&osc);
          process_osc_msg(osc, len);
        }
      }
    }
    handleBlink();
  }

  // close the UDP socket
  close(fd);

  return 0;
}
