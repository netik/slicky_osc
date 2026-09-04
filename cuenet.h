#ifndef CUENET_H
#define CUENET_H

#include <stdbool.h>
#include <sys/select.h>
#include <sys/time.h>

typedef struct {
    int valid;
    int system_id;
    int cue_group;
    int cue1;
    int cue2;
    unsigned long seq1;
    unsigned long seq2;
} cuenet_state_t;

typedef void (*cuenet_change_fn)(const cuenet_state_t *state, void *ctx);

bool cuenet_enabled(void);
bool cuenet_start(int system_id, int cue_group, int http_port,
                  cuenet_change_fn on_change, void *ctx);
void cuenet_stop(void);

void cuenet_prepare_select(fd_set *readfds, int *max_fd, struct timeval *timeout);
void cuenet_handle_select(const fd_set *readfds);
void cuenet_poll_peers(void);

int cuenet_poll_interval_ms(void);
long cuenet_ms_now(void);

const cuenet_state_t *cuenet_get_state(void);
bool cuenet_set_cue(int cue_num, int value);
int cuenet_http_port(void);

#endif /* CUENET_H */
