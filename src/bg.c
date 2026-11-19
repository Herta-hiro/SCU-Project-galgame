#include "bg.h"

#include "raylib.h"
#include <stdio.h>
#include <string.h>

#define SCREEN_W 1280
#define SCREEN_H  720
#define MAX_BG    12
#define BG_NAME   32

/* 背景排查日志。默认关：打开时每帧都会写盘，只在找“为什么这张图没显示”时用。
   打开方式：编译时加 -DBG_DEBUG=1（注意是 BG_DEBUG），或改下面的宏。 */
#ifndef BG_DEBUG
#define BG_DEBUG 0
#endif

#if BG_DEBUG
#define BG_LOG(...) do {                       \
        FILE *f_ = fopen("bg_debug.log", "a"); \
        if (f_) { fprintf(f_, __VA_ARGS__); fclose(f_); } \
    } while (0)
#else
#define BG_LOG(...) ((void)0)
#endif

typedef struct {
    char      name[BG_NAME];
    Texture2D tex;
    int       ok;
} BgSlot;

static BgSlot g_slots[MAX_BG];
static int    g_count;

void bg_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_count = 0;
}

void bg_shutdown(void)
{
    for (int i = 0; i < g_count; i++)
        if (g_slots[i].ok) UnloadTexture(g_slots[i].tex);
    g_count = 0;
}

static Texture2D *bg_get(const char *name)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_slots[i].name, name) == 0) {
            BG_LOG("cache hit name=%s ok=%d\n", name, g_slots[i].ok);
            return g_slots[i].ok ? &g_slots[i].tex : NULL;
        }

    if (g_count >= MAX_BG) return NULL;

    BgSlot *slot = &g_slots[g_count++];
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->ok = 0;

    const char *exts[] = { "png", "jpg" };
    for (int e = 0; e < 2; e++) {
        char path[256];
        snprintf(path, sizeof(path), "data/backgrounds/%s.%s", name, exts[e]);
        int exists = FileExists(path);
        BG_LOG("try name=%s path=%s exists=%d\n", name, path, exists);
        if (!exists) continue;

        slot->tex = LoadTexture(path);
        BG_LOG("  LoadTexture id=%d w=%d h=%d\n", slot->tex.id, slot->tex.width, slot->tex.height);
        if (slot->tex.id > 0) {
            SetTextureFilter(slot->tex, TEXTURE_FILTER_BILINEAR);
            slot->ok = 1;
            return &slot->tex;
        }
    }

    BG_LOG("FAILED name=%s\n", name);
    return NULL;
}

/* 按 cover 方式铺满：取较大的缩放比，居中裁切。 */
static void draw_cover(Texture2D t)
{
    float sx = (float)SCREEN_W / (float)t.width;
    float sy = (float)SCREEN_H / (float)t.height;
    float s  = sx > sy ? sx : sy;

    Rectangle src = {
        (t.width  - SCREEN_W / s) * 0.5f,
        (t.height - SCREEN_H / s) * 0.5f,
        SCREEN_W / s,
        SCREEN_H / s
    };
    Rectangle dst = { 0, 0, (float)SCREEN_W, (float)SCREEN_H };
    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

int bg_draw(const char *name)
{
    if (!name || !*name) return 0;

    Texture2D *t = bg_get(name);
    if (!t) {
        BG_LOG("bg_draw(%s) -> NULL\n", name);
        return 0;
    }

    draw_cover(*t);

    /* 压暗蒙版：上淡下深，让对话框和选项一直读得清。 */
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H,
                           (Color){ 6, 9, 14, 30 },
                           (Color){ 4, 6, 10, 80 });
    return 1;
}