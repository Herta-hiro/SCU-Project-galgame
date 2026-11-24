#include "audio.h"

#include "raylib.h"
#include <stdio.h>

#define BGM_OGG "data/audio/bgm_rain.ogg"
#define BGM_WAV "data/audio/bgm_rain.wav"
#define SE_CLICK "data/audio/se_click.wav"
#define SE_PAGE  "data/audio/se_page.wav"

static Music g_bgm;
static Sound g_click;
static Sound g_page;

static int g_device;        /* 声卡是否初始化成功 */
static int g_bgm_ok;
static int g_click_ok;
static int g_page_ok;
static int g_bgm_on = 1;

/* 环境音音量。以前是 0.35，实测在 60 秒循环的雨声上会显得"一直有个沙沙声"，
   尤其标题界面就开始响。压到 0.22：还在，但退到台词后面去了。 */
#define BGM_VOLUME 0.22f

void audio_init(void)
{
    g_device = g_bgm_ok = g_click_ok = g_page_ok = 0;
    g_bgm_on = 1;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        printf("音频设备不可用，静音运行\n");
        return;
    }
    g_device = 1;

    /* 雨声压得很低：它是环境音，不能盖过台词。
       优先 ogg（仓库里存的是压缩版），没有就退回 wav（gen_audio.py 的原始输出）。 */
    const char *bgm = FileExists(BGM_OGG) ? BGM_OGG : (FileExists(BGM_WAV) ? BGM_WAV : NULL);
    if (bgm) {
        g_bgm = LoadMusicStream(bgm);
        g_bgm.looping = true;
        SetMusicVolume(g_bgm, BGM_VOLUME);
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

void audio_set_bgm_enabled(int on)
{
    g_bgm_on = on ? 1 : 0;
    if (!g_device || !g_bgm_ok) return;

    if (g_bgm_on) {
        PlayMusicStream(g_bgm);
        SetMusicVolume(g_bgm, BGM_VOLUME);
    } else {
        SetMusicVolume(g_bgm, 0.0f);   /* 只压音量不停流：停了再开会从断点接，听感突兀 */
    }
}

int audio_bgm_enabled(void)
{
    return g_bgm_on;
}
