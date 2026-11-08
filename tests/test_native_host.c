#include "native_apps.h"
#include "desktop_host.h"
#include "media_player_panel.h"
#include "audio_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void little16(FILE *f, unsigned value) { fputc(value & 255, f); fputc((value >> 8) & 255, f); }
static void little32(FILE *f, unsigned value) { little16(f, value); little16(f, value >> 16); }
int main(void) {
    char root[] = "/tmp/os999-native-host-XXXXXX";
    assert(mkdtemp(root) && !volume_seed(root));
    char path[OSPI_PATH]; snprintf(path, sizeof path, "%s/Documents/test.wav", root);
    FILE *f = fopen(path, "wb"); assert(f);
    fwrite("RIFF", 1, 4, f); little32(f, 36 + 1600); fwrite("WAVEfmt ", 1, 8, f);
    little32(f, 16); little16(f, 1); little16(f, 1); little32(f, 8000);
    little32(f, 16000); little16(f, 2); little16(f, 16);
    fwrite("data", 1, 4, f); little32(f, 1600);
    for (int i = 0; i < 800; i++) little16(f, i % 32 < 16 ? 3000 : 62536);
    assert(!fclose(f));
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    desktop_host_init(root); assert(audio_service_is_initialized());
    audio_service_pause_output(1);
    Session *s = session_create(root); assert(s);
    assert(!native_app_open(s, NATIVE_MUSIC));
    assert(media_panel_active() && s->media_focused);
    int noted = 0;
    for (int i = 0; i < s->nrecent; i++)
        if (s->recent[i].application && native_catalog_id(root, s->recent[i].path) == NATIVE_MUSIC) noted = 1;
    assert(noted);
    desktop_host_end_session(s);
    assert(!audio_service_is_initialized());
    printf("Native music launcher opens real player and records Recent Applications; %s\n", root);
    return 0;
}
