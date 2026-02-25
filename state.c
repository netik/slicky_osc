#include "state.h"
#include "config.h"
#include "led.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static int current_color = 0x000000;
static int blinks_to_do = 0;
static bool blink_on_change = true;
static bool blinking = false;
static bool last_state = true;

void state_process_osc_msg(tosc_message *osc, int len, bool debug) {
    char cmd[MAX_STR];

    if (debug) {
        log_debug("Received OSC message: [%i bytes] %s %s",
                  len, tosc_getAddress(osc), tosc_getFormat(osc));
    }

    strncpy(cmd, tosc_getAddress(osc), MAX_STR - 1);
    cmd[MAX_STR - 1] = '\0';
    log_info("cmd: %s", cmd);

    if (strncmp(cmd, "/setcolorint", MAX_STR) == 0) {
        int newcolor = tosc_getNextInt32(osc);
        current_color = newcolor;
        blinking = false;
        led_set_rgb((color_rgb_t)newcolor);
        if (blink_on_change) {
            blinks_to_do = 6;
        }
    }

    if (strncmp(cmd, "/setcolorhex", MAX_STR) == 0) {
        const char *hexstr = tosc_getNextString(osc);
        if (hexstr != NULL) {
            char *end;
            errno = 0;
            unsigned long u = strtoul(hexstr, &end, 16);
            if (errno == 0 && (*end == '\0' || *end == ' ') && u <= 0xFFFFFFu) {
                int newcolor = (int)u;
                current_color = newcolor;
                blinking = false;
                led_set_rgb((color_rgb_t)newcolor);
            }
        }
        if (blink_on_change) {
            blinks_to_do = 6;
        }
    }

    if (strncmp(cmd, "/blink", MAX_STR) == 0) {
        int blinkparam = tosc_getNextInt32(osc);
        if (blinkparam > 0) {
            log_info("set blink on");
            blinking = true;
            if (current_color == 0) {
                current_color = 0xFF0000;
            }
        } else {
            log_info("set blink off");
            blinking = false;
            led_set_rgb((color_rgb_t)current_color);
        }
    }

    if (strncmp(cmd, "/blink_on_change", MAX_STR) == 0) {
        int blinkparam = tosc_getNextInt32(osc);
        if (blinkparam > 0) {
            log_info("set blink_on_change on");
            blink_on_change = true;
        } else {
            log_info("set blink_on_change off");
            blink_on_change = false;
        }
    }
}

void state_handle_blink(void) {
    if (blinking || blinks_to_do > 0) {
        if (last_state) {
            led_set_rgb((color_rgb_t)current_color);
        } else {
            led_set_rgb((color_rgb_t)0x000000);
        }
        last_state = !last_state;
        if (blinks_to_do > 0) {
            blinks_to_do--;
        }
    } else {
        led_set_rgb((color_rgb_t)current_color);
    }
}

void state_send_osc_status(int fd, const struct sockaddr *peer, socklen_t peer_len, bool debug) {
    char outbuf[128];
    uint32_t n;
    ssize_t sent;

    n = tosc_writeMessage(outbuf, sizeof(outbuf), "/status/color", "i", current_color);
    if (n > 0) {
        if (debug) {
            log_debug("status: /status/color %d (0x%06x)", current_color, current_color & 0xFFFFFF);
        }
        sent = sendto(fd, outbuf, (size_t)n, 0, peer, peer_len);
        if (sent != (ssize_t)n && debug) {
            log_debug("send_osc_status: sendto %zd of %u", (long)sent, (unsigned)n);
        }
    }

    n = tosc_writeMessage(outbuf, sizeof(outbuf), "/status/blinking", "i", blinking ? 1 : 0);
    if (n > 0) {
        if (debug) {
            log_debug("status: /status/blinking %d", blinking ? 1 : 0);
        }
        sent = sendto(fd, outbuf, (size_t)n, 0, peer, peer_len);
        if (sent != (ssize_t)n && debug) {
            log_debug("send_osc_status: sendto %zd of %u", (long)sent, (unsigned)n);
        }
    }

    n = tosc_writeMessage(outbuf, sizeof(outbuf), "/status/blink_on_change", "i", blink_on_change ? 1 : 0);
    if (n > 0) {
        if (debug) {
            log_debug("status: /status/blink_on_change %d", blink_on_change ? 1 : 0);
        }
        sent = sendto(fd, outbuf, (size_t)n, 0, peer, peer_len);
        if (sent != (ssize_t)n && debug) {
            log_debug("send_osc_status: sendto %zd of %u", (long)sent, (unsigned)n);
        }
    }
}
