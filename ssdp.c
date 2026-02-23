#include "ssdp.h"
#include "config.h"
#include "cli.h"
#include "log.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static char *get_local_ip_address(void) {
    static char ip_buffer[INET_ADDRSTRLEN];

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_debug("get_local_ip_address: socket failed, fallback to 127.0.0.1");
        strcpy(ip_buffer, "127.0.0.1");
        return ip_buffer;
    }

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(53);
    remote_addr.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        log_debug("connect failed, fallback to 127.0.0.1");
        close(sock);
        strcpy(ip_buffer, "127.0.0.1");
        return ip_buffer;
    }

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock, (struct sockaddr *)&local_addr, &addr_len) < 0) {
        log_debug("getsockname failed: %s", strerror(errno));
        close(sock);
        strcpy(ip_buffer, "127.0.0.1");
        return ip_buffer;
    }

    close(sock);

    if (inet_ntop(AF_INET, &local_addr.sin_addr, ip_buffer, INET_ADDRSTRLEN) == NULL) {
        strcpy(ip_buffer, "127.0.0.1");
    }

    return ip_buffer;
}

void ssdp_send_announcement(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_error("Failed to create SSDP socket");
        return;
    }

    int ttl = 4;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        log_error("Failed to set multicast TTL");
        close(sock);
        return;
    }

    char *local_ip = get_local_ip_address();

    char buffer[2048];
    int len = snprintf(buffer, sizeof(buffer),
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: %s:%d\r\n"
        "CACHE-CONTROL: max-age=1800\r\n"
        "LOCATION: http://%s:%d/osc-cue-description.xml\r\n"
        "NT: urn:schemas-upnp-org:service:OSC_CUE:1\r\n"
        "NTS: ssdp:alive\r\n"
        "SERVER: OSC_Cue_Light/1.0 UPnP/1.0\r\n"
        "USN: uuid:OSC_Cue_Light_1.0::urn:schemas-upnp-org:service:OSC_CUE:1\r\n"
        "\r\n",
        SSDP_MULTICAST_IP, SSDP_PORT, local_ip, port);

    struct sockaddr_in ssdp_addr;
    memset(&ssdp_addr, 0, sizeof(ssdp_addr));
    ssdp_addr.sin_family = AF_INET;
    ssdp_addr.sin_port = htons(SSDP_PORT);
    ssdp_addr.sin_addr.s_addr = inet_addr(SSDP_MULTICAST_IP);

    ssize_t sent = sendto(sock, buffer, len, 0, (struct sockaddr *)&ssdp_addr, sizeof(ssdp_addr));

    if (sent > 0) {
        if (cli_debug()) {
            log_debug("SSDP service announcement sent for port %d (%zd bytes)", port, sent);
            log_debug("Local IP: %s, Service: urn:schemas-upnp-org:service:OSC:1", local_ip);
        }
    } else {
        log_error("Failed to send SSDP announcement: %s", strerror(errno));
    }

    close(sock);
}

void ssdp_announce_service(int port) {
    ssdp_send_announcement(port);
    if (cli_debug()) {
        log_info("SSDP service announced: urn:schemas-upnp-org:service:OSC:1 on port %d", port);
    }
}
