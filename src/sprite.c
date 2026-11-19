#include "sprite.h"

#include "raylib.h"
#include <stdio.h>
#include <string.h>

#define SCREEN_W 1280
#define SCREEN_H  720
#define MAX_SPR   12
#define SPR_NAME  64

typedef struct {
    char      name[SPR_NAME];
    Texture2D tex;
    int       ok;
} SprSlot;

static SprSlot g_slots[MAX_SPR];
static int     g_count;

void sprite_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_count = 0;
}

void sprite_shutdown(void)
{
    for (int i = 0; i < g_count; i++)
        if (g_slots[i].ok) UnloadTexture(g_slots[i].tex);
    g_count = 0;
}

static Texture2D *load_slot(const char *char_id, const char *expr)
{
    char want[SPR_NAME];
    if (expr && *expr) snprintf(want, sizeof(want), "%s_%s", char_id, expr);
    else               snprintf(want, sizeof(want), "%s_normal", char_id);

    for (int i = 0; i < g_count; i++)
        if (strcmp(g_slots[i].name, want) == 0)
            return g_slots[i].ok ? &g_slots[i].tex : NULL;

    if (g_count >= MAX_SPR) return NULL;

    /* 依次尝试：<id>_<expr>.png → <id>.png → <id>_normal.png */
    char cand[3][SPR_NAME];
    snprintf(cand[0], SPR_NAME, "%s", want);
    snprintf(cand[1], SPR_NAME, "%s", char_id);
    snprintf(cand[2], SPR_NAME, "%s_normal", char_id);
    int n = (strcmp(cand[0], cand[2]) == 0) ? 1 : 3;

    for (int c = 0; c < n; c++) {
        char path[256];
        snprintf(path, sizeof(path), "data/sprites/%s.png", cand[c]);
        if (!FileExists(path)) continue;

        SprSlot *slot = &g_slots[g_count++];
        snprintf(slot->name, sizeof(slot->name), "%s", want);
        slot->tex = LoadTexture(path);
        if (slot->tex.id > 0) {
            SetTextureFilter(slot->tex, TEXTURE_FILTER_BILINEAR);
            slot->ok = 1;
            return &slot->tex;
        }
        slot->ok = 0;
        return NULL;
    }

    /* 记一笔“查过了”，下次不再翻文件 */
    if (g_count < MAX_SPR) {
        SprSlot *slot = &g_slots[g_count++];
        snprintf(slot->name, sizeof(slot->name), "%s", want);
        slot->ok = 0;
    }
    return NULL;
}

int sprite_draw(const char *spec)
{
    if (!spec || !*spec) return 0;
    if (strcmp(spec, "off") == 0) return 0;

    char char_id[SPR_NAME] = { 0 };
    char expr[SPR_NAME]    = { 0 };

    const char *colon = strchr(spec, ':');
    if (colon) {
        int n = (int)(colon - spec);
        if (n >= SPR_NAME) n = SPR_NAME - 1;
        memcpy(char_id, spec, (size_t)n);
        snprintf(expr, sizeof(expr), "%s", colon + 1);
    } else {
        snprintf(char_id, sizeof(char_id), "%s", spec);
    }

    Texture2D *t = load_slot(char_id, expr);
    if (!t || t->id == 0) return 0;

    /* 按屏幕高度缩放，底部对齐、靠右；略往下探出画面，避免“贴地”感 */
    float scale = (float)SCREEN_H * 1.02f / (float)t->height;
    float w = (float)t->width * scale;
    float h = (float)t->height * scale;
    float x = (float)SCREEN_W - w - 70.0f;
    float y = (float)SCREEN_H - h + 26.0f;

    DrawTextureEx(*t, (Vector2){ x, y }, 0.0f, scale, WHITE);
    return 1;
}