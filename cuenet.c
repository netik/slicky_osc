/*
 * cuenet — join the esp_cue_light LAN via mDNS and sync /api/cues state.
 *
 * Browse _cuelight._tcp peers, poll GET /api/cues, push POST on local changes,
 * and serve GET/POST /api/cues so ESP boards can sync with this host.
 */

#include "cuenet.h"
#include "config.h"
#include "log.h"
#include <arpa/inet.h>
#include <dns_sd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVICE_TYPE "_cuelight._tcp."
#define SERVICE_DOMAIN "local."
#define API_PATH "/api/cues"
#define RESPONSE_MAX 512
#define HTTP_TIMEOUT_MS 400
#define MAX_PEERS 16
#define MAX_SD_REFS 32
#define HTTP_BACKLOG 4
#define HTTP_READ_MAX 512
#define MDNS_SERVICE_NAME "SlickyOSC"

typedef struct {
    int in_use;
    char name[64];
    char host[256];
    uint16_t port;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    int has_addr;
    int txt_system_id;
    int txt_cue_group;
    int has_txt_system_id;
    int has_txt_cue_group;
} Peer;

typedef struct {
    DNSServiceRef ref;
} SdRefSlot;

static bool g_enabled = false;
static int g_system_id = CUENET_DEFAULT_SYSTEM_ID;
static int g_cue_group = CUENET_DEFAULT_CUE_GROUP;
static int g_http_port = CUENET_HTTP_PORT;
static int g_poll_ms = CUENET_POLL_MS;

static Peer g_peers[MAX_PEERS];
static SdRefSlot g_sd_refs[MAX_SD_REFS];
static int g_sd_ref_count = 0;
static DNSServiceRef g_browse_ref = NULL;
static DNSServiceRef g_register_ref = NULL;
static int g_listen_fd = -1;
static cuenet_state_t g_state;
static cuenet_change_fn g_on_change = NULL;
static void *g_on_change_ctx = NULL;
static int g_suppress_push = 0;
static long g_next_poll_ms = 0;

static int add_sd_ref(DNSServiceRef ref) {
    if (g_sd_ref_count >= MAX_SD_REFS) {
        return 0;
    }
    g_sd_refs[g_sd_ref_count++].ref = ref;
    return 1;
}

static void remove_sd_ref(DNSServiceRef ref) {
    for (int i = 0; i < g_sd_ref_count; ++i) {
        if (g_sd_refs[i].ref == ref) {
            g_sd_refs[i] = g_sd_refs[g_sd_ref_count - 1];
            --g_sd_ref_count;
            return;
        }
    }
}

static Peer *alloc_peer(const char *name) {
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (g_peers[i].in_use && strcmp(g_peers[i].name, name) == 0) {
            return &g_peers[i];
        }
    }
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (!g_peers[i].in_use) {
            memset(&g_peers[i], 0, sizeof(g_peers[i]));
            g_peers[i].in_use = 1;
            snprintf(g_peers[i].name, sizeof(g_peers[i].name), "%s", name);
            return &g_peers[i];
        }
    }
    return NULL;
}

static void drop_peer(const char *name) {
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (g_peers[i].in_use && strcmp(g_peers[i].name, name) == 0) {
            g_peers[i].in_use = 0;
            log_info("cue peer left: %s", name);
            return;
        }
    }
}

static int peer_matches_filter(const Peer *peer) {
    if (peer->has_txt_system_id && peer->txt_system_id != g_system_id) {
        return 0;
    }
    if (peer->has_txt_cue_group && peer->txt_cue_group != g_cue_group) {
        return 0;
    }
    return 1;
}

static int parse_uint_json(const char *json, const char *key, unsigned long *out) {
    char pattern[32];
    const char *p;
    char *end;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (p == NULL) {
        return 0;
    }
    p += strlen(pattern);
    *out = strtoul(p, &end, 10);
    return end != p;
}

static int parse_state_json(const char *json, cuenet_state_t *state) {
    unsigned long v;

    memset(state, 0, sizeof(*state));
    if (!parse_uint_json(json, "system_id", &v)) {
        return 0;
    }
    state->system_id = (int)v;
    if (!parse_uint_json(json, "cue_group", &v)) {
        return 0;
    }
    state->cue_group = (int)v;
    if (!parse_uint_json(json, "cue1", &v)) {
        return 0;
    }
    state->cue1 = (int)v;
    if (!parse_uint_json(json, "cue2", &v)) {
        return 0;
    }
    state->cue2 = (int)v;
    if (!parse_uint_json(json, "seq1", &state->seq1)) {
        return 0;
    }
    if (!parse_uint_json(json, "seq2", &state->seq2)) {
        return 0;
    }
    state->valid = 1;
    return 1;
}

static int build_state_json(char *buf, size_t size, const cuenet_state_t *state) {
    return snprintf(buf, size,
                    "{\"system_id\":%d,\"cue_group\":%d,\"cue1\":%d,\"cue2\":%d,"
                    "\"seq1\":%lu,\"seq2\":%lu}",
                    state->system_id, state->cue_group, state->cue1, state->cue2,
                    state->seq1, state->seq2);
}

static int is_seq_newer(unsigned long incoming, unsigned long current) {
    return incoming != current && (incoming - current) < 0x80000000UL;
}

static void notify_change(void) {
    if (g_on_change != NULL) {
        g_on_change(&g_state, g_on_change_ctx);
    }
}

static int apply_incoming_state(const cuenet_state_t *incoming) {
    int changed = 0;

    if (!incoming->valid) {
        return 0;
    }
    if (incoming->system_id != g_system_id || incoming->cue_group != g_cue_group) {
        return 0;
    }

    if (!g_state.valid) {
        g_state = *incoming;
        notify_change();
        return 1;
    }

    if (is_seq_newer(incoming->seq1, g_state.seq1)) {
        g_state.cue1 = incoming->cue1;
        g_state.seq1 = incoming->seq1;
        changed = 1;
    }
    if (is_seq_newer(incoming->seq2, g_state.seq2)) {
        g_state.cue2 = incoming->cue2;
        g_state.seq2 = incoming->seq2;
        changed = 1;
    }

    if (changed) {
        notify_change();
    }
    return changed;
}

static int resolve_peer_addr(Peer *peer) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_str[8];

    peer->has_addr = 0;
    peer->addr_len = 0;

    snprintf(port_str, sizeof(port_str), "%u", peer->port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(peer->host, port_str, &hints, &res) != 0 || res == NULL) {
        return 0;
    }

    if ((size_t)res->ai_addrlen > sizeof(peer->addr)) {
        freeaddrinfo(res);
        return 0;
    }

    memcpy(&peer->addr, res->ai_addr, res->ai_addrlen);
    peer->addr_len = (socklen_t)res->ai_addrlen;
    peer->has_addr = 1;
    freeaddrinfo(res);
    return 1;
}

static int http_exchange(const Peer *peer, const char *method, const char *body,
                         char *response, size_t response_size) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    int sock = -1;
    char request[512];
    char port_str[8];
    ssize_t n;
    int status = 0;
    const struct timeval tv = {
        .tv_sec = HTTP_TIMEOUT_MS / 1000,
        .tv_usec = (HTTP_TIMEOUT_MS % 1000) * 1000,
    };

    if (peer != NULL && peer->has_addr) {
        sock = socket(peer->addr.ss_family, SOCK_STREAM, 0);
        if (sock >= 0) {
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (connect(sock, (const struct sockaddr *)&peer->addr, peer->addr_len) != 0) {
                close(sock);
                sock = -1;
            }
        }
    }

    if (sock < 0 && peer != NULL) {
        snprintf(port_str, sizeof(port_str), "%u", peer->port);

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(peer->host, port_str, &hints, &res) != 0) {
            return 0;
        }

        for (rp = res; rp != NULL; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0) {
                continue;
            }
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
                break;
            }
            close(sock);
            sock = -1;
        }
        freeaddrinfo(res);
    }

    if (sock < 0) {
        return 0;
    }

    if (body != NULL) {
        snprintf(request, sizeof(request),
                 "%s " API_PATH " HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 method, peer != NULL ? peer->host : "localhost", strlen(body), body);
    } else {
        snprintf(request, sizeof(request),
                 "GET " API_PATH " HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 peer != NULL ? peer->host : "localhost");
    }

    if (send(sock, request, strlen(request), 0) < 0) {
        close(sock);
        return 0;
    }

    n = recv(sock, response, (ssize_t)(response_size - 1), 0);
    close(sock);
    if (n <= 0) {
        return 0;
    }
    response[n] = '\0';

    if (sscanf(response, "HTTP/%*d.%*d %d", &status) != 1) {
        return 0;
    }

    return status;
}

static int fetch_cues(const Peer *peer, char *body, size_t body_size) {
    char response[RESPONSE_MAX];
    const char *body_start;
    int status;

    status = http_exchange(peer, "GET", NULL, response, sizeof(response));
    if (status != 200) {
        return 0;
    }

    body_start = strstr(response, "\r\n\r\n");
    if (body_start == NULL) {
        return 0;
    }
    snprintf(body, body_size, "%s", body_start + 4);
    return 1;
}

static void push_state_to_peer(Peer *peer) {
    char body[128];
    char response[RESPONSE_MAX];
    int status;

    if (!peer->in_use || peer->host[0] == '\0' || !peer_matches_filter(peer)) {
        return;
    }

    build_state_json(body, sizeof(body), &g_state);
    status = http_exchange(peer, "POST", body, response, sizeof(response));
    if (status == 200) {
        log_debug("cue push accepted by %s", peer->name);
    } else if (status == 409) {
        log_debug("cue push ignored by %s (stale)", peer->name);
    } else if (status > 0) {
        log_debug("cue push to %s: HTTP %d", peer->name, status);
    }
}

static void push_state_to_peers(void) {
    if (g_suppress_push > 0) {
        return;
    }
    for (int i = 0; i < MAX_PEERS; ++i) {
        push_state_to_peer(&g_peers[i]);
    }
}

static void apply_txt(Peer *peer, uint16_t txt_len, const unsigned char *txt) {
    char buf[16];
    const void *value;
    uint8_t len;

    if (TXTRecordContainsKey(txt_len, txt, "system_id")) {
        value = TXTRecordGetValuePtr(txt_len, txt, "system_id", &len);
        if (value != NULL && len < sizeof(buf)) {
            memcpy(buf, value, len);
            buf[len] = '\0';
            peer->txt_system_id = atoi(buf);
            peer->has_txt_system_id = 1;
        }
    }
    if (TXTRecordContainsKey(txt_len, txt, "cue_group")) {
        value = TXTRecordGetValuePtr(txt_len, txt, "cue_group", &len);
        if (value != NULL && len < sizeof(buf)) {
            memcpy(buf, value, len);
            buf[len] = '\0';
            peer->txt_cue_group = atoi(buf);
            peer->has_txt_cue_group = 1;
        }
    }
}

static void poll_peer(Peer *peer) {
    char body[256];
    cuenet_state_t remote;

    if (!peer->in_use || peer->host[0] == '\0' || !peer_matches_filter(peer)) {
        return;
    }
    if (fetch_cues(peer, body, sizeof(body)) && parse_state_json(body, &remote)) {
        g_suppress_push++;
        apply_incoming_state(&remote);
        g_suppress_push--;
    }
}

static void DNSSD_API resolve_reply(DNSServiceRef sd_ref, DNSServiceFlags flags,
                                    uint32_t interface_index,
                                    DNSServiceErrorType error_code,
                                    const char *full_name, const char *host,
                                    uint16_t port, uint16_t txt_len,
                                    const unsigned char *txt_record,
                                    void *context) {
    Peer *peer = (Peer *)context;

    (void)flags;
    (void)interface_index;
    (void)full_name;

    if (error_code != kDNSServiceErr_NoError || peer == NULL) {
        if (peer != NULL) {
            log_warn("cue resolve failed for %s: error %d", peer->name, (int)error_code);
        }
        goto done;
    }

    snprintf(peer->host, sizeof(peer->host), "%s", host);
    peer->port = ntohs(port);
    apply_txt(peer, txt_len, txt_record);
    resolve_peer_addr(peer);

    if (peer_matches_filter(peer)) {
        log_info("cue peer joined: %s at %s:%u (system=%d group=%d)",
                 peer->name, peer->host, peer->port,
                 peer->has_txt_system_id ? peer->txt_system_id : 0,
                 peer->has_txt_cue_group ? peer->txt_cue_group : 0);
        poll_peer(peer);
    }

done:
    DNSServiceRefDeallocate(sd_ref);
    remove_sd_ref(sd_ref);
}

static void start_resolve(uint32_t interface_index, const char *name,
                          const char *regtype, const char *domain) {
    Peer *peer = alloc_peer(name);
    DNSServiceRef resolve_ref = NULL;
    DNSServiceErrorType err;

    if (peer == NULL) {
        return;
    }

    err = DNSServiceResolve(&resolve_ref, 0, interface_index, name, regtype,
                            domain, resolve_reply, peer);
    if (err != kDNSServiceErr_NoError) {
        log_warn("DNSServiceResolve failed: %d", (int)err);
        return;
    }

    if (!add_sd_ref(resolve_ref)) {
        DNSServiceRefDeallocate(resolve_ref);
    }
}

static void DNSSD_API browse_reply(DNSServiceRef sd_ref, DNSServiceFlags flags,
                                   uint32_t interface_index,
                                   DNSServiceErrorType error_code,
                                   const char *service_name,
                                   const char *regtype, const char *domain,
                                   void *context) {
    (void)sd_ref;
    (void)context;

    if (error_code != kDNSServiceErr_NoError) {
        return;
    }

    if (flags & kDNSServiceFlagsAdd) {
        if (strcmp(service_name, MDNS_SERVICE_NAME) == 0 ||
            strstr(service_name, "SlickyOSC") != NULL) {
            return;
        }
        log_debug("cue discovered: %s", service_name);
        start_resolve(interface_index, service_name, regtype, domain);
    } else {
        drop_peer(service_name);
    }
}

static void drain_ref(DNSServiceRef ref) {
    if (ref == NULL) {
        return;
    }

    const int fd = DNSServiceRefSockFD(ref);
    if (fd < 0) {
        return;
    }

    for (;;) {
        fd_set ready;
        struct timeval zero = {0, 0};

        FD_ZERO(&ready);
        FD_SET(fd, &ready);
        if (select(fd + 1, &ready, NULL, NULL, &zero) <= 0) {
            break;
        }
        if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError) {
            break;
        }
    }
}

static void process_mdns(int timeout_ms) {
    fd_set readfds;
    int max_fd = -1;
    struct timeval tv;
    DNSServiceRef pending[MAX_SD_REFS];
    int pending_count = 0;

    FD_ZERO(&readfds);

    if (g_browse_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_browse_ref);
        if (fd >= 0) {
            FD_SET(fd, &readfds);
            if (fd > max_fd) {
                max_fd = fd;
            }
        }
    }

    if (g_register_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_register_ref);
        if (fd >= 0) {
            FD_SET(fd, &readfds);
            if (fd > max_fd) {
                max_fd = fd;
            }
        }
    }

    pending_count = g_sd_ref_count;
    for (int i = 0; i < pending_count; ++i) {
        pending[i] = g_sd_refs[i].ref;
        const int fd = DNSServiceRefSockFD(pending[i]);
        if (fd >= 0) {
            FD_SET(fd, &readfds);
            if (fd > max_fd) {
                max_fd = fd;
            }
        }
    }

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (max_fd >= 0) {
        select(max_fd + 1, &readfds, NULL, NULL, &tv);
    } else if (timeout_ms > 0) {
        usleep((useconds_t)timeout_ms * 1000U);
        return;
    }

    if (g_browse_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_browse_ref);
        if (fd >= 0 && FD_ISSET(fd, &readfds)) {
            drain_ref(g_browse_ref);
        }
    }

    if (g_register_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_register_ref);
        if (fd >= 0 && FD_ISSET(fd, &readfds)) {
            drain_ref(g_register_ref);
        }
    }

    for (int i = 0; i < pending_count; ++i) {
        const int fd = DNSServiceRefSockFD(pending[i]);
        if (fd >= 0 && FD_ISSET(fd, &readfds)) {
            drain_ref(pending[i]);
        }
    }
}

static int http_server_start(int port) {
    struct sockaddr_in sin;
    int opt = 1;

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        log_error("cue HTTP: socket failed: %s", strerror(errno));
        return 0;
    }

    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons((uint16_t)port);

    if (bind(g_listen_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        log_error("cue HTTP: bind port %d failed: %s", port, strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return 0;
    }

    if (listen(g_listen_fd, HTTP_BACKLOG) < 0) {
        log_error("cue HTTP: listen failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return 0;
    }

    fcntl(g_listen_fd, F_SETFL, O_NONBLOCK);
    return 1;
}

static const char *find_body(const char *request) {
    return strstr(request, "\r\n\r\n");
}

static void http_handle_client(int client_fd) {
    char request[HTTP_READ_MAX];
    ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
    cuenet_state_t incoming;
    char json[128];
    char response[256];
    int status_code;
    const char *body;
    const char *body_start;

    if (n <= 0) {
        close(client_fd);
        return;
    }
    request[n] = '\0';

    if (strncmp(request, "GET " API_PATH, strlen("GET " API_PATH)) == 0) {
        build_state_json(json, sizeof(json), &g_state);
        snprintf(response, sizeof(response),
                 "HTTP/1.0 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 strlen(json), json);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return;
    }

    if (strncmp(request, "POST " API_PATH, strlen("POST " API_PATH)) == 0) {
        body_start = find_body(request);
        if (body_start == NULL) {
            status_code = 409;
        } else {
            body = body_start + 4;
            if (!parse_state_json(body, &incoming)) {
                status_code = 409;
            } else {
                g_suppress_push++;
                status_code = apply_incoming_state(&incoming) ? 200 : 409;
                g_suppress_push--;
            }
        }

        snprintf(response, sizeof(response),
                 "HTTP/1.0 %d %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: 9\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "{\"ok\":%d}",
                 status_code, status_code == 200 ? "OK" : "Conflict",
                 status_code == 200 ? 1 : 0);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return;
    }

    snprintf(response, sizeof(response),
             "HTTP/1.0 404 Not Found\r\n"
             "Content-Length: 0\r\n"
             "Connection: close\r\n"
             "\r\n");
    send(client_fd, response, strlen(response), 0);
    close(client_fd);
}

static void http_server_accept(void) {
    for (;;) {
        int client = accept(g_listen_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }
            log_warn("cue HTTP accept failed: %s", strerror(errno));
            break;
        }
        http_handle_client(client);
    }
}

static int mdns_register_service(int port) {
    TXTRecordRef txt;
    char sys_buf[8];
    char grp_buf[8];
    DNSServiceErrorType err;

    TXTRecordCreate(&txt, 0, NULL);
    snprintf(sys_buf, sizeof(sys_buf), "%d", g_system_id);
    snprintf(grp_buf, sizeof(grp_buf), "%d", g_cue_group);
    TXTRecordSetValue(&txt, "system_id", (uint8_t)strlen(sys_buf), sys_buf);
    TXTRecordSetValue(&txt, "cue_group", (uint8_t)strlen(grp_buf), grp_buf);

    err = DNSServiceRegister(&g_register_ref, 0, kDNSServiceInterfaceIndexAny,
                             MDNS_SERVICE_NAME, SERVICE_TYPE, SERVICE_DOMAIN,
                             NULL, htons((uint16_t)port),
                             TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt),
                             NULL, NULL);
    TXTRecordDeallocate(&txt);

    if (err != kDNSServiceErr_NoError) {
        log_error("DNSServiceRegister failed: %d", (int)err);
        return 0;
    }

    if (!add_sd_ref(g_register_ref)) {
        DNSServiceRefDeallocate(g_register_ref);
        g_register_ref = NULL;
        return 0;
    }

    return 1;
}

long cuenet_ms_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

bool cuenet_enabled(void) {
    return g_enabled;
}

bool cuenet_start(int system_id, int cue_group, int http_port,
                  cuenet_change_fn on_change, void *ctx) {
    DNSServiceErrorType err;

    g_system_id = system_id;
    g_cue_group = cue_group;
    g_http_port = http_port;
    g_on_change = on_change;
    g_on_change_ctx = ctx;

    memset(&g_state, 0, sizeof(g_state));
    g_state.system_id = system_id;
    g_state.cue_group = cue_group;
    g_state.valid = 1;

    if (!http_server_start(http_port)) {
        return false;
    }

    err = DNSServiceBrowse(&g_browse_ref, 0, kDNSServiceInterfaceIndexAny,
                           SERVICE_TYPE, SERVICE_DOMAIN, browse_reply, NULL);
    if (err != kDNSServiceErr_NoError) {
        log_error("DNSServiceBrowse failed: %d", (int)err);
        close(g_listen_fd);
        g_listen_fd = -1;
        return false;
    }

    if (!mdns_register_service(http_port)) {
        DNSServiceRefDeallocate(g_browse_ref);
        g_browse_ref = NULL;
        close(g_listen_fd);
        g_listen_fd = -1;
        return false;
    }

    drain_ref(g_browse_ref);
    g_next_poll_ms = cuenet_ms_now() + g_poll_ms;
    g_enabled = true;

    log_info("Cue network: browsing %s%s, serving %s on port %d (system=%d group=%d)",
             SERVICE_TYPE, SERVICE_DOMAIN, API_PATH, http_port, system_id, cue_group);
    return true;
}

void cuenet_stop(void) {
    if (!g_enabled) {
        return;
    }

    if (g_register_ref != NULL) {
        DNSServiceRefDeallocate(g_register_ref);
        g_register_ref = NULL;
    }
    if (g_browse_ref != NULL) {
        DNSServiceRefDeallocate(g_browse_ref);
        g_browse_ref = NULL;
    }
    while (g_sd_ref_count > 0) {
        DNSServiceRefDeallocate(g_sd_refs[g_sd_ref_count - 1].ref);
        --g_sd_ref_count;
    }
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    memset(g_peers, 0, sizeof(g_peers));
    g_enabled = false;
    g_on_change = NULL;
    g_on_change_ctx = NULL;
}

void cuenet_prepare_select(fd_set *readfds, int *max_fd, struct timeval *timeout) {
    int mdns_cap;
    long now;
    long until_poll;
    int wait_ms;

    if (!g_enabled) {
        return;
    }

    if (g_listen_fd >= 0) {
        FD_SET(g_listen_fd, readfds);
        if (g_listen_fd > *max_fd) {
            *max_fd = g_listen_fd;
        }
    }

    if (g_browse_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_browse_ref);
        if (fd >= 0) {
            FD_SET(fd, readfds);
            if (fd > *max_fd) {
                *max_fd = fd;
            }
        }
    }

    if (g_register_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_register_ref);
        if (fd >= 0) {
            FD_SET(fd, readfds);
            if (fd > *max_fd) {
                *max_fd = fd;
            }
        }
    }

    for (int i = 0; i < g_sd_ref_count; ++i) {
        const int fd = DNSServiceRefSockFD(g_sd_refs[i].ref);
        if (fd >= 0) {
            FD_SET(fd, readfds);
            if (fd > *max_fd) {
                *max_fd = fd;
            }
        }
    }

    now = cuenet_ms_now();
    until_poll = (now >= g_next_poll_ms) ? 0 : (g_next_poll_ms - now);
    mdns_cap = (g_poll_ms > 50) ? 50 : g_poll_ms;
    wait_ms = (until_poll > mdns_cap) ? mdns_cap : (int)until_poll;
    if (wait_ms < (int)timeout->tv_sec * 1000 + (int)(timeout->tv_usec / 1000)) {
        timeout->tv_sec = wait_ms / 1000;
        timeout->tv_usec = (wait_ms % 1000) * 1000;
    }
}

void cuenet_handle_select(const fd_set *readfds) {
    if (!g_enabled) {
        return;
    }

    if (g_listen_fd >= 0 && FD_ISSET(g_listen_fd, readfds)) {
        http_server_accept();
    }

    if (g_browse_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_browse_ref);
        if (fd >= 0 && FD_ISSET(fd, readfds)) {
            drain_ref(g_browse_ref);
        }
    }

    if (g_register_ref != NULL) {
        const int fd = DNSServiceRefSockFD(g_register_ref);
        if (fd >= 0 && FD_ISSET(fd, readfds)) {
            drain_ref(g_register_ref);
        }
    }

    for (int i = 0; i < g_sd_ref_count; ++i) {
        const int fd = DNSServiceRefSockFD(g_sd_refs[i].ref);
        if (fd >= 0 && FD_ISSET(fd, readfds)) {
            drain_ref(g_sd_refs[i].ref);
        }
    }
}

void cuenet_poll_peers(void) {
    if (!g_enabled) {
        return;
    }

    for (int i = 0; i < MAX_PEERS; ++i) {
        poll_peer(&g_peers[i]);
    }
    g_next_poll_ms = cuenet_ms_now() + g_poll_ms;
}

int cuenet_poll_interval_ms(void) {
    return g_poll_ms;
}

const cuenet_state_t *cuenet_get_state(void) {
    return &g_state;
}

bool cuenet_set_cue(int cue_num, int value) {
    int changed = 0;
    int clamped = value ? 1 : 0;

    if (!g_enabled || !g_state.valid) {
        return false;
    }

    if (cue_num == 1) {
        if (g_state.cue1 != clamped) {
            g_state.cue1 = clamped;
            g_state.seq1++;
            changed = 1;
        }
    } else if (cue_num == 2) {
        if (g_state.cue2 != clamped) {
            g_state.cue2 = clamped;
            g_state.seq2++;
            changed = 1;
        }
    } else {
        return false;
    }

    if (changed) {
        notify_change();
        push_state_to_peers();
    }
    return changed != 0;
}

int cuenet_http_port(void) {
    return g_http_port;
}
