#ifndef STRING_H
#define STRING_H

#include "types.h"

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memmove(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strstr(const char *haystack, const char *needle);
void int_to_str(int num, char *out);
char *strncat(char *dest, const char *src, size_t n);
static inline char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (*s == '\0') return NULL;
        s++;
    }
    return (char*)s;
}
#endif
