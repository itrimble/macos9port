#define SDL_MAIN_HANDLED
#include "mac_runtime.h"
#include <SDL.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
static int created, freed, shown, creates_fail, shows_fail, last_x, last_y;
static SDL_Cursor *selected;
static uint8_t copied[64], ram[4096];
static int halted;
static SDL_Cursor *fake_create(const Uint8 *d,const Uint8 *m,int w,int h,int x,int y) {
    assert(w==16 && h==16); last_x=x; last_y=y;
    memcpy(copied,d,32); memcpy(copied+32,m,32);
    return creates_fail ? NULL : (SDL_Cursor *)(uintptr_t)(++created + 100);
}
static void fake_set(SDL_Cursor *p) { selected=p; }
static void fake_free(SDL_Cursor *p) { if(p) { assert(p!=selected); freed++; } }
static int fake_show(int v) { shown++; return shows_fail ? -1 : v; }
static SDL_Cursor *fake_default(void) { return (SDL_Cursor *)(uintptr_t)99; }
uint8_t *mac_cpu_ram(MacCpu *cpu) { (void)cpu; return ram; }
size_t mac_cpu_ram_size(const MacCpu *cpu) { (void)cpu; return sizeof ram; }
void mac_cpu_halt(MacCpu *cpu,const char *error) { (void)cpu; (void)error; halted=1; }
#define SDL_GetError() "synthetic SDL failure"
#define SDL_CreateCursor fake_create
#define SDL_SetCursor fake_set
#define SDL_FreeCursor fake_free
#define SDL_ShowCursor fake_show
#define SDL_GetDefaultCursor fake_default
#define OSPI_VIDEO_CONVERSION_TEST
#define OSPI_CURSOR_TEST
#include "../src/runtime_video.c"
int main(void) {
    MacRuntime rt={0}; rt.cpu=(MacCpu *)(uintptr_t)1; rt.cursor_valid=1;
    rt.cursor[0]=0x80; rt.cursor[32]=0xc0; rt.cursor[65]=3; rt.cursor[67]=7;
    HostCursor host={.previous=(SDL_Cursor *)(uintptr_t)42,.previous_visibility=0,.visibility=-1};
    assert(!sync_cursor(&rt,&host)); assert(created==1 && shown==1);
    assert(last_x==7 && last_y==3 && copied[0]==0x80 && copied[32]==0xc0);
    assert(!sync_cursor(&rt,&host)); assert(created==1 && shown==1);
    rt.cursor[0]=0x40; assert(!sync_cursor(&rt,&host)); assert(created==2 && freed==1);
    rt.cursor_obscured=1; assert(!sync_cursor(&rt,&host)); assert(host.visibility==0);
    rt.cursor_obscured=0; ram[0x8d0]=255; ram[0x8d1]=255;
    assert(!sync_cursor(&rt,&host)); assert(host.visibility==0);
    ram[0x8d0]=ram[0x8d1]=0; assert(!sync_cursor(&rt,&host)); assert(host.visibility==1);
    int before=created; rt.cursor[67]=16; assert(sync_cursor(&rt,&host)==-1);
    assert(halted && created==before); rt.cursor[67]=255; assert(sync_cursor(&rt,&host)==-1);
    rt.cursor[67]=7; rt.cursor[1]=1; creates_fail=1;
    assert(sync_cursor(&rt,&host)==-1 && created==before && freed==1);
    creates_fail=0; assert(!sync_cursor(&rt,&host));
    shows_fail=1; rt.cursor_obscured=1; assert(sync_cursor(&rt,&host)==-1);
    shows_fail=0; restore_cursor(&host); assert(!host.guest && selected==host.previous && freed==created);
    puts("host cursor: mocked lifecycle and projection checks passed");
}
