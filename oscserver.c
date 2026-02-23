#include "config.h"
#include "cli.h"
#include "ssdp.h"
#include "led.h"
#include "state.h"
#include "log.h"
#include "tinyosc.h"
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

static volatile bool keep_running = true;

static void sigint_handler(int sig) {
    (void)sig;
    keep_running = false;
}

int main(int argc, char *argv[]) {
    char buffer[2048];
    time_t last_ssdp = 0;

    log_set_level(LOG_INFO);
    cli_parse_arguments(argc, argv);
    signal(SIGINT, sigint_handler);

    led_init(cli_test_mode());
    if (cli_test_mode()) {
        log_info("Test mode: running without USB (no HID init or device open).");
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(cli_port());
    sin.sin_addr.s_addr = INADDR_ANY;
    bind(fd, (struct sockaddr *)&sin, sizeof(sin));

    log_info("Server is now listening on port %d UDP, feedback on port %d, advertising SSDP on port %d.",
             cli_port(), FEEDBACK_PORT, SSDP_PORT);
    log_info("Press Ctrl+C to stop.");

    log_debug("announce_ssdp_service start");
    ssdp_announce_service(cli_port());
    log_debug("announce_ssdp_service done");

    time_t last_status_time = 0;
    struct sockaddr_in last_status_peer;
    bool have_status_peer = false;

    while (keep_running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);

        struct timeval timeout = {1, 0};
        log_debug("select start");

        if (select(fd + 1, &read_set, NULL, NULL, &timeout) > 0) {
            struct sockaddr sa;
            socklen_t sa_len = sizeof(struct sockaddr_in);
            int len;

            while ((len = (int)recvfrom(fd, buffer, sizeof(buffer), 0, &sa, &sa_len)) > 0) {
                bool was_empty = !have_status_peer;
                memcpy(&last_status_peer, &sa, sizeof(last_status_peer));
                have_status_peer = true;
                if (was_empty) {
                    last_status_time = time(NULL);
                }

                if (tosc_isBundle(buffer)) {
                    tosc_bundle bundle;
                    tosc_parseBundle(&bundle, buffer, len);
                    tosc_message osc;
                    while (tosc_getNextMessage(&bundle, &osc)) {
                        state_process_osc_msg(&osc, len, cli_debug());
                    }
                } else {
                    tosc_message osc;
                    tosc_parseMessage(&osc, buffer, len);
                    state_process_osc_msg(&osc, len, cli_debug());
                }

                struct sockaddr_in feedback_dest;
                memcpy(&feedback_dest, &sa, sizeof(feedback_dest));
                feedback_dest.sin_port = htons(FEEDBACK_PORT);
                state_send_osc_status(fd, (struct sockaddr *)&feedback_dest, sizeof(feedback_dest), cli_debug());
            }
            log_debug("select done");
        }

        time_t now = time(NULL);

        if (have_status_peer && now - last_status_time >= STATUS_INTERVAL) {
            struct sockaddr_in feedback_dest = last_status_peer;
            feedback_dest.sin_port = htons(FEEDBACK_PORT);
            state_send_osc_status(fd, (struct sockaddr *)&feedback_dest, sizeof(feedback_dest), cli_debug());
            last_status_time = now;
        }

        if (now - last_ssdp >= SSDP_INTERVAL) {
            log_debug("current_time: %ld, last_ssdp_announcement: %ld", (long)now, (long)last_ssdp);
            ssdp_send_announcement(cli_port());
            last_ssdp = now;
            if (cli_debug()) {
                log_debug("Sent periodic SSDP announcement for port %d", cli_port());
            }
        }

        log_debug("handleBlink start");
        state_handle_blink();
        log_debug("handleBlink done");
    }

    close(fd);
    return 0;
}
