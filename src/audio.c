#include "audio.h"

#include "raylib.h"
#include <stdio.h>

#define BGM_FILE "data/audio/bgm_rain.wav"
#define SE_CLICK "data/audio/se_click.wav"
#define SE_PAGE  "data/audio/se_page.wav"

static Music g_bgm;
static Sound g_click;
static Sound g_page;

static int g_device;        /* 声卡是否初始化成功 */
static int g_bgm_ok;
static int g_click_ok;
static int g_page_ok;

void audio_init(void)
{
    g_device = g_bgm_ok = g_click_ok = g_page_ok = 0;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        printf("音频设备不可用，静音运行\n");
        return;
    }
    g_device = 1;

    /* 雨声压得很低：它是环境音，不能盖过台词 */
    if (FileExists(BGM_FILE)) {
        g_bgm = LoadMusicStream(BGM_FILE);
        g_bgm.looping = true;
        SetMusicVolume(g_bgm, 0.35f);
        PlayMusicStream(g_bgm);
        g_bgm_ok = 1;
    }

    if (FileExists(SE_CLICK)) {
        g_click = LoadSound(SE_CLICK);
        SetSoundVolume(g_click, 0.45f);
        g_click_ok = 1;
    }

    if (FileExists(SE_PAGE)) {
        g_page = LoadSound(SE_PAGE);
        SetSoundVolume(g_page, 0.40f);
        g_page_ok = 1;
    }
}

void audio_update(void)
{
    /* 音乐流必须每帧喂，喂慢了会断音 */
    if (g_device && g_bgm_ok) UpdateMusicStream(g_bgm);
}

void audio_shutdown(void)
{
    if (!g_device) return;

    if (g_bgm_ok)   { UnloadMusicStream(g_bgm); g_bgm_ok = 0; }
    if (g_click_ok) { UnloadSound(g_click);     g_click_ok = 0; }
    if (g_page_ok)  { UnloadSound(g_page);      g_page_ok = 0; }

    CloseAudioDevice();
    g_device = 0;
}

void audio_se_click(void)
{
    if (g_device && g_click_ok) PlaySound(g_click);
}

void audio_se_page(void)
{
    if (g_device && g_page_ok) PlaySound(g_page);
}
