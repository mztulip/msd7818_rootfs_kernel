#include <stdlib.h>
#include "opl3.h"
typedef struct { opl3_chip c; int reg; } W;
void *OPLCreate(int clock, int rate) { W *w = calloc(1, sizeof(W)); OPL3_Reset(&w->c, rate); return w; }
void OPLDestroy(void *p) { free(p); }
int OPLWrite(void *p, int a, int v) { W *w = p; if (a == 0) w->reg = v; else OPL3_WriteReg(&w->c, w->reg, v); return 0; }
void YM3812UpdateOne(void *p, short *buf, int n) { W *w = p; short st[2 * 1024]; int i; OPL3_GenerateStream(&w->c, st, n); for (i = 0; i < n; ++i) buf[i] = st[2 * i]; }
