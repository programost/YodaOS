#ifndef SHELL_H
#define SHELL_H

void shell_run(void);
void shell_dispatch_line(const char *cmd);
void shell_run_script(const char *path);

#endif
