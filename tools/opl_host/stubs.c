#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
void *Z_Malloc(int size, int tag, void *user) { return malloc(size); }
void Z_Free(void *p) { free(p); }
void I_Error(char *e, ...) { va_list a; va_start(a, e); vfprintf(stderr, e, a); exit(1); }
