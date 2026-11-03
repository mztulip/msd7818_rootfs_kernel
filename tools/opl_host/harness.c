// render Doom music through i_oplmusic.c + opl_fmopl.c to raw 48 kHz s16
// stereo; OUT "rt": no file, paced to real time (synth-only load on the board)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "doomtype.h"
#include "i_sound.h"
#include "m_argv.h"
#include "opl.h"

extern music_module_t music_opl_module;
int snd_samplerate = 48000;
int snd_musicdevice = SNDDEVICE_GENMIDI;
static FILE *wad; static int nlumps, diroff;
typedef struct { int pos, size; char name[8]; } lump_t;
static lump_t *dir;

static int Find(const char *n)
{
    int i;
    for (i = 0; i < nlumps; ++i) if (!strncasecmp(dir[i].name, n, 8)) return i;
    return -1;
}
void *W_CacheLumpName(char *name, int tag)
{
    int i = Find(name); void *p;
    if (i < 0) { fprintf(stderr, "no lump %s\n", name); exit(1); }
    p = malloc(dir[i].size); fseek(wad, dir[i].pos, SEEK_SET); fread(p, 1, dir[i].size, wad);
    return p;
}
void W_ReleaseLumpName(char *name) { }
int W_LumpLength(int i) { return dir[i].size; }

int main(int argc, char **argv)
{
    int32_t acc[480 * 2]; int16_t out[480 * 2];
    int secs = atoi(argv[3]), i, b; void *song; FILE *o; int li; void *d;
    struct timespec t0, t1, next; double busy = 0; int rt;
    myargc = argc; myargv = argv;
    wad = fopen(argv[1], "rb"); char hdr[12]; fread(hdr, 1, 12, wad);
    nlumps = *(int *)(hdr + 4); diroff = *(int *)(hdr + 8);
    dir = malloc(nlumps * sizeof(lump_t)); fseek(wad, diroff, SEEK_SET);
    for (i = 0; i < nlumps; ++i) fread(&dir[i], 16, 1, wad);
    if (!music_opl_module.Init()) { fprintf(stderr, "init\n"); return 1; }
    music_opl_module.SetMusicVolume(127);
    li = Find(argv[2]); d = W_CacheLumpName(argv[2], 0);
    song = music_opl_module.RegisterSong(d, dir[li].size);
    music_opl_module.PlaySong(song, 1);
    rt = !strcmp(argv[4], "rt");
    o = rt ? NULL : fopen(argv[4], "wb");
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (b = 0; b < secs * 100; ++b)
    {
        memset(acc, 0, sizeof(acc));
        clock_gettime(CLOCK_MONOTONIC, &t0);
        OPL_MixMusic(acc, 480);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        busy += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        for (i = 0; i < 960; ++i) { int v = acc[i]; out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
        if (rt)
        {
            next.tv_nsec += 10000000;
            if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; ++next.tv_sec; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
        else
        {
            fwrite(out, 2, 960, o);
        }
    }
    if (o != NULL) fclose(o);
    fprintf(stderr, "%d s rendered in %.3f s CPU (%.2f %% realtime)\n", secs, busy, busy * 100 / secs);
    return 0;
}
