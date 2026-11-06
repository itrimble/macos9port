#include "desktop_theme.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct {
    const char *id, *name;
    uint32_t colors[THEME_ROLE_COUNT];
} DesktopTheme;
/* Original palettes. Names carry the requested visual direction only: no
 * wallpaper, round-button, graph-paper or imported theme-file behavior here. */
#define RGB(v) (0xff000000u | 0x##v##u)
#define COLORS(panel,menu,text,disabled,frame,track,a,b,accent,top,bottom) \
    {RGB(panel),RGB(menu),RGB(text),RGB(disabled),RGB(frame),RGB(track),RGB(a),RGB(b),RGB(accent),RGB(top),RGB(bottom)}
static const DesktopTheme themes[] = {
    {"platinum", "Platinum", COLORS(FFFFFF,EEEEEE,000000,8A8A8A,000000,D2D2D2,BEBEBE,A3A3A3,9A98EE,5B71A2,56709C)},
    {"graphite", "Graphite", COLORS(F4F5F6,DEE1E4,20262C,666E77,303840,C6CCD2,C1C7CD,AAB2BB,718699,667685,465462)},
    {"titanium-night", "Titanium Night", COLORS(20252D,2B323D,F1F4F8,A8B1BD,C9D3DF,38414E,37414E,29313C,81B9D6,202C40,111722)},
    {"toybox", "Toybox", COLORS(FFFDF2,FFF0C7,292540,71657B,45355C,E4D8C0,F1D79F,E7C588,E69B35,8BBBC1,577FA6)},
    {"sketchbook", "Sketchbook", COLORS(FFFDF4,F0ECDE,34352E,77796A,54594B,DEDDCC,DEDAC7,D0CCB6,7D9C82,B7C0AA,85947C)},
    {"classic-1-bit", "Classic 1-Bit", COLORS(FFFFFF,FFFFFF,000000,666666,000000,FFFFFF,FFFFFF,FFFFFF,000000,FFFFFF,FFFFFF)},
    {"color-seven", "Color Seven", COLORS(FFF9EE,ECE5D8,242033,6E6478,3D344D,CFC6BB,D8D0C5,C0B5A9,7C78BC,7E89AD,535E88)},
    {"blueberry", "Blueberry", COLORS(F4FAFF,DBEAF7,17344D,566E83,285675,C4D9E9,C6DDED,A8C5DC,498FC1,5D9FCC,295D8E)},
    {"tangerine", "Tangerine", COLORS(FFFAF1,FBE8D0,4B2C17,80674F,7B4E2B,EBD2B6,EACDA8,DAB78A,CC792E,DEAD78,B87642)},
    {"lime", "Lime", COLORS(FAFFF1,E9F2D2,2C3D1B,65744B,526C35,D3DDBB,D8E5BB,BBD098,85AB47,A5C67B,688F4F)},
    {"grape", "Grape", COLORS(FBF6FF,ECE0F4,3B2450,796286,66487D,D7C8E2,DCC9E9,C2ABD4,9B6DBC,AB8FC5,705389)},
    {"strawberry", "Strawberry", COLORS(FFF7F9,F8E0E6,4D2632,89636F,7A4255,E4C6D0,EBCBD6,D5A9BA,C36D8A,D19AAB,9B6077)},
    {"pinstripe", "Pinstripe", COLORS(FAFCFD,E5EBF0,283746,647481,465D70,CBD5DE,DCE5ED,C4D1DD,7B9DB8,9EAFBF,70879C)},
    {"sunflower", "Sunflower", COLORS(FFFCED,F8EDBA,413A1B,7C724E,71632E,E4D69F,EBDA9C,D5BF73,B19537,D2BE6D,9F904A)},
    {"periwinkle", "Periwinkle", COLORS(FAF9FF,E7E4F7,302B4B,716A88,5C5481,CFC9E8,D6D0EF,BEB5DE,8880C8,8C89BD,646496)},
    {"midnight-pi", "Midnight Pi", COLORS(192A32,223940,EAF8F6,9AB8BA,B9D9D7,304B52,2F4C55,223940,73B9B4,18343E,0E1D29)}
};
#undef COLORS
#undef RGB
static int current;
static char preference[4096];

int desktop_theme_count(void) { return (int)(sizeof themes / sizeof *themes); }
const char *desktop_theme_name(int index) {
    return index >= 0 && index < desktop_theme_count() ? themes[index].name : "";
}
int desktop_theme_current(void) { return current; }
void desktop_theme_shutdown(void) { current = 0; preference[0] = 0; }
uint32_t desktop_theme_color(int role) {
    return role >= 0 && role < THEME_ROLE_COUNT ? themes[current].colors[role] : 0xff000000u;
}

int desktop_theme_init(const char *path) {
    desktop_theme_shutdown();
    if (!path) return 0;
    size_t length = strnlen(path, sizeof preference);
    if (!length || length >= sizeof preference) return -1;
    memcpy(preference, path, length + 1);
    int fd = open(preference, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return -1; }
    if (st.st_size < 1 || st.st_size > 64) { close(fd); return 0; }
    char value[65] = {0};
    ssize_t n = read(fd, value, (size_t)st.st_size);
    int closed = close(fd);
    if (n != st.st_size || closed) return -1;
    if (memchr(value, 0, (size_t)n)) return 0;
    while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r')) value[--n] = 0;
    for (int i = 0; i < desktop_theme_count(); i++)
        if (!strcmp(value, themes[i].id)) { current = i; break; }
    return 0;
}

int desktop_theme_select(int index) {
    if (index < 0 || index >= desktop_theme_count()) return -1;
    if (preference[0]) {
        char temp[4096];
        int n = snprintf(temp, sizeof temp, "%s.XXXXXX", preference);
        if (n <= 0 || (size_t)n >= sizeof temp) return -1;
        int fd = mkstemp(temp);
        if (fd < 0) return -1;
        const char *id = themes[index].id;
        size_t length = strlen(id);
        int ok = write(fd, id, length) == (ssize_t)length && write(fd, "\n", 1) == 1 && fsync(fd) == 0;
        if (close(fd)) ok = 0;
        if (ok && rename(temp, preference)) ok = 0;
        if (!ok) { unlink(temp); return -1; }
    }
    current = index;
    return 0;
}
int desktop_theme_next(void) { return desktop_theme_select((current + 1) % desktop_theme_count()); }
