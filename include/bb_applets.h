#ifndef BB_APPLETS_H
#define BB_APPLETS_H

#include "types.h"

#define BB_MAX_ARGS 32
#define BB_ARGPOOL 4096

int bb_tokenize(const char *line, char *argv[], int max_args, char *pool, size_t pool_sz);
int bb_hybrid_read(const char *path, uint8_t *buf, size_t cap, size_t *out_len);
int bb_hybrid_write(const char *path, const uint8_t *data, size_t len);
int bb_kernel_dispatch(int argc, char **argv);
int bb_applet_main(int argc, char **argv);

#endif
