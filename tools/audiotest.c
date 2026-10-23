/* Writes every synthesized sound effect to a WAV file (little-endian host assumed). */
#include "../source/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *NAMES[SFX_COUNT] = {
    "move", "select", "build", "upgrade", "sell", "error", "arrow", "cannon", "frost", "explode",
    "kill", "leak", "wave", "boss", "clear", "start", "pause", "gameover", "win"
};

static void w32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    audio_init();
    for (int id = 0; id < SFX_COUNT; id++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%02d_%s.wav", dir, id, NAMES[id]);
        FILE *f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        int n = audio_len(id), bytes = n * 2, vol = audio_volume(id);
        /* bake the suggested playback volume in so the preview matches the game's mix */
        fwrite("RIFF", 1, 4, f); w32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
        w32(f, 16); w16(f, 1); w16(f, 1); w32(f, SFX_RATE); w32(f, SFX_RATE * 2); w16(f, 2); w16(f, 16);
        fwrite("data", 1, 4, f); w32(f, bytes);
        const int16_t *d = audio_data(id);
        for (int i = 0; i < n; i++) { int16_t s = (int16_t)(d[i] * vol / 255); fwrite(&s, 2, 1, f); }
        fclose(f);
        printf("%02d %-9s %5d samples  %.2fs\n", id, NAMES[id], n, (float)n / SFX_RATE);
    }
    return 0;
}
