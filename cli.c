#include "cli.h"
#include "config.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>
#include <errno.h>

static int port = 9000;
static bool debug_mode = false;
static bool test_mode = false;
static bool cue_net_enabled = false;
static int cue_system_id = CUENET_DEFAULT_SYSTEM_ID;
static int cue_group = CUENET_DEFAULT_CUE_GROUP;
static int cue_http_port = CUENET_HTTP_PORT;
static int cue_led = 1;

void cli_print_usage(const char *program_name) {
    printf("\nUsage: %s [OPTIONS]\n\n", program_name);
    printf("This program listens for OSC messages on the specified port and controls LED colors.\n\n");
    printf("Options:\n");
    printf("  -d, --debug     Enable debug mode\n");
    printf("  -h, --help      Show this help message\n");
    printf("  -p, --port      Specify port number (default: 9000)\n");
    printf("  -t, --test      Test mode: run without USB device (no HID init or I/O)\n");
    printf("  -C, --cue-net   Join cue-light mDNS network (GET/POST /api/cues)\n");
    printf("  -s, --system-id N   Cue network system ID (default: %d)\n", CUENET_DEFAULT_SYSTEM_ID);
    printf("  -g, --cue-group N   Cue network group (default: %d)\n", CUENET_DEFAULT_CUE_GROUP);
    printf("      --cue-port N    HTTP port for /api/cues (default: %d)\n", CUENET_HTTP_PORT);
    printf("      --cue-led N     Drive LED from cue 1 or 2 (default: 1)\n");
    printf("\n");
    printf("The OSC messages are sent to the /setcolorint and /setcolorhex addresses.\n");
    printf("\n");
    printf("OSC Message Formats:\n\n");
    printf("  /setcolorint nnnnn  expects a 32-bit int.\n");
    printf("  /setcolorhex nnnnn  expects a string to convert to a 32-bit rgb color in hex.\n");
    printf("  /blink n            expects a 32-bit integer. Any value > 0 enables blinking.\n");
    printf("  /blink_on_change n  expects a 32-bit integer. Any value > 0 enables blinking on color change.\n");
    printf("  /setcue1 n          cue 1: 0=RED, 1=GREEN (syncs when --cue-net is on)\n");
    printf("  /setcue2 n          cue 2: 0=RED, 1=GREEN (syncs when --cue-net is on)\n");
    printf("\n");
    printf("Status (server -> client, port %d):\n", FEEDBACK_PORT);
    printf("  Reply: sent for each received packet. Periodic: every 1 second to last sender.\n");
    printf("  /status/color nnnn        32-bit RGB color\n");
    printf("  /status/blinking 0|1      continuous blink on (1) or off (0)\n");
    printf("  /status/blink_on_change 0|1  blink on color change (1) or off (0)\n");
    printf("\n");
    printf("Press Ctrl+C to stop.\n");
}

void cli_parse_arguments(int argc, char *argv[]) {
    int opt;
    const char *short_options = "dhtp:Cs:g:";
    struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"test", no_argument, 0, 't'},
        {"port", required_argument, 0, 'p'},
        {"cue-net", no_argument, 0, 'C'},
        {"system-id", required_argument, 0, 's'},
        {"cue-group", required_argument, 0, 'g'},
        {"cue-port", required_argument, 0, 1000},
        {"cue-led", required_argument, 0, 1001},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                debug_mode = true;
                log_set_level(LOG_DEBUG);
                break;
            case 'h':
                cli_print_usage(argv[0]);
                exit(0);
                break;
            case 't':
                test_mode = true;
                break;
            case 'p': {
                char *end;
                errno = 0;
                long p = strtol(optarg, &end, 10);
                if (errno != 0 || *end != '\0' || p <= 0 || p > 65535) {
                    fprintf(stderr, "Error: Port must be between 1 and 65535\n");
                    exit(1);
                }
                port = (int)p;
                break;
            }
            case 'C':
                cue_net_enabled = true;
                break;
            case 's': {
                char *end;
                errno = 0;
                long v = strtol(optarg, &end, 10);
                if (errno != 0 || *end != '\0' || v < 0 || v > 65535) {
                    fprintf(stderr, "Error: system-id must be 0-65535\n");
                    exit(1);
                }
                cue_system_id = (int)v;
                break;
            }
            case 'g': {
                char *end;
                errno = 0;
                long v = strtol(optarg, &end, 10);
                if (errno != 0 || *end != '\0' || v < 0 || v > 65535) {
                    fprintf(stderr, "Error: cue-group must be 0-65535\n");
                    exit(1);
                }
                cue_group = (int)v;
                break;
            }
            case 1000: {
                char *end;
                errno = 0;
                long p = strtol(optarg, &end, 10);
                if (errno != 0 || *end != '\0' || p <= 0 || p > 65535) {
                    fprintf(stderr, "Error: cue-port must be between 1 and 65535\n");
                    exit(1);
                }
                cue_http_port = (int)p;
                break;
            }
            case 1001: {
                char *end;
                errno = 0;
                long v = strtol(optarg, &end, 10);
                if (errno != 0 || *end != '\0' || (v != 1 && v != 2)) {
                    fprintf(stderr, "Error: cue-led must be 1 or 2\n");
                    exit(1);
                }
                cue_led = (int)v;
                break;
            }
            default:
                cli_print_usage(argv[0]);
                exit(1);
        }
    }
}

int cli_port(void) { return port; }
bool cli_debug(void) { return debug_mode; }
bool cli_test_mode(void) { return test_mode; }
bool cli_cue_net_enabled(void) { return cue_net_enabled; }
int cli_cue_system_id(void) { return cue_system_id; }
int cli_cue_group(void) { return cue_group; }
int cli_cue_http_port(void) { return cue_http_port; }
int cli_cue_led(void) { return cue_led; }
