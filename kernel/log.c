#include "log.h"
#include "drivers.h"    
#include "string.h"
#include "fs.h"

#define LOG_BUFF_SZ 65536

static char log_buff[LOG_BUFF_SZ];
static size_t log_pos = 0;
int log_overflow = 0; 

static void log_append(const char* str) {
    size_t len = strlen(str);
    if (log_pos + len >= LOG_BUFF_SZ) {
        if (log_pos < LOG_BUFF_SZ) {
            size_t avail = LOG_BUFF_SZ - log_pos - 1;
            if (avail > 0) {
                strncpy(log_buff + log_pos, str, avail);
                log_pos += avail;
                log_buff[log_pos] = '\0';
            }
        }
        log_overflow = 1;
        return;
    }
    strcpy(log_buff + log_pos, str);
    log_pos += len;
}

void dbstring(const char** Type, const char* Msg) {
    const char* prefix = *Type;

    char buffer[256];
    buffer[0] = '\0';   

    strcat(buffer, prefix);
    strcat(buffer, " ");
    strcat(buffer, Msg);
    strcat(buffer, "\n");

    log_append(buffer);
}
int log_save_to_file(){
    const char* filename = "shlog.log";
    if (!fs_is_mounted()) {
        return -1;
    }
    if (fs_exists(filename)){
        fs_delete(filename);
    }
    if (fs_create(filename,0) != 0){
        return -1;
    }
    if (fs_write(filename, (const uint8_t*)log_buff, (uint32_t)log_pos) !=0 ){
        return -1;
    }
    if (log_overflow) {
        const char* overflow_msg = "\n[!!! LOG BUFFER OVERFLOW !!!]\n";
        fs_write(filename, (const uint8_t*)overflow_msg, (uint32_t)strlen(overflow_msg));
    }
    return 0;
}