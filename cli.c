#include "cli.h"
#include "config.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>

static int port = 9000;
static bool debug_mode = false;
static bool test_mode = false;

void cli_print_usage(const char *program_name) {
    printf("\nUsage: %s [OPTIONS]\n\n", program_name);
    printf("This program listens for OSC messages on the specified port and controls LED colors.\n\n");
    printf("Options:\n");
    printf("  -d, --debug     Enable debug mode\n");
    printf("  -h, --help      Show this help message\n");
    printf("  -p, --port      Specify port number (default: 9000)\n");
    printf("  -t, --test      Test mode: run without USB device (no HID init or I/O)\n");
    printf("\n");
    printf("The OSC messages are sent to the /setcolorint and /setcolorhex addresses.\n");
    printf("\n");
    printf("OSC Message Formats:\n\n");
    printf("  /setcolorint nnnnn  expects a 32-bit int.\n");
    printf("  /setcolorhex nnnnn  expects a string to convert to a 32-bit rgb color in hex.\n");
    printf("  /blink n            expects a 32-bit integer. Any value > 0 enables blinking.\n");
    printf("  /blink_on_change n  expects a 32-bit integer. Any value > 0 enables blinking on color change.\n");
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
    const char *short_options = "dhtp:";
    struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"test", no_argument, 0, 't'},
        {"port", required_argument, 0, 'p'},
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
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Error: Port must be between 1 and 65535\n");
                    exit(1);
                }
                break;
            default:
                cli_print_usage(argv[0]);
                exit(1);
        }
    }
}

int cli_port(void) { return port; }
bool cli_debug(void) { return debug_mode; }
bool cli_test_mode(void) { return test_mode; }
