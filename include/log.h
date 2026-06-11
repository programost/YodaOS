#ifndef LOG_H
#define LOG_H

#include "types.h"
#include "string.h"

void dbstring(const char** Type, const char* Msg);
int log_save_to_file();

#endif