#ifndef CLI_H
#define CLI_H

#include <stdbool.h>

void cli_parse_arguments(int argc, char *argv[]);
void cli_print_usage(const char *program_name);

int cli_port(void);
bool cli_debug(void);
bool cli_test_mode(void);

#endif /* CLI_H */
