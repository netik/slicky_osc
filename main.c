#include "hidapi.h"
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include "tinyosc.h"

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

int main() {
    int res = hid_init();

    tosc_message osc; // declare the TinyOSC structure
    char buffer[1024]; // declare a buffer into which to read the socket contents
    int len = 0; // the number of bytes read from the socket

    if (res < 0) {
        printf("Error: Problem initializing the hidapi library.");
    }

    // open device
    hidDevice = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    
    // serve    
    while ((len = READ_BYTES_FROM_SOCKET(buffer)) > 0) {
    // parse the buffer contents (the raw OSC bytes)
    // a return value of 0 indicates no error
        if (!tosc_readMessage(&osc, buffer, len)) {
            printf("Received OSC message: [%i bytes] %s %s ",
                len, // the number of bytes in the OSC message
                tosc_getAddress(&osc), // the OSC address string, e.g. "/button1"
                tosc_getFormat(&osc)); // the OSC format string, e.g. "f"
                
            for (int i = 0; osc.format[i] != '\0'; i++) {
                switch (osc.format[i]) {
                    case 'f': printf("%g ", tosc_getNextFloat(&osc)); break;
                    case 'i': printf("%i ", tosc_getNextInt32(&osc)); break;
                    // returns NULL if the buffer length is exceeded
                    case 's': printf("%s ", tosc_getNextString(&osc)); break;
                    default: continue;
                }
            }
            printf("\n");
        }
    }
}
