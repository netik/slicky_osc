#ifndef CLI_H
#define CLI_H

#include <stdbool.h>

void cli_parse_arguments(int argc, char *argv[]);
void cli_print_usage(const char *program_name);

int cli_port(void);
bool cli_debug(void);
bool cli_test_mode(void);

bool cli_cue_net_enabled(void);
int cli_cue_system_id(void);
int cli_cue_group(void);
int cli_cue_http_port(void);
int cli_cue_led(void);

#endif /* CLI_H */
