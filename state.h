#ifndef STATE_H
#define STATE_H

#include "tinyosc.h"
#include <stdbool.h>
#include <sys/socket.h>

struct sockaddr;

void state_process_osc_msg(tosc_message *osc, int len, bool debug);
void state_handle_blink(void);
void state_send_osc_status(int fd, const struct sockaddr *peer, socklen_t peer_len, bool debug);

#endif /* STATE_H */
