#include "voice.h"

#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 一句一个文件：data/voice/<节点>_<行号>.ogg
   引擎里 line_index 就是节点内 lines[] 的下标（旁白也占位），
   tools/gen_voice.py 按同一套规则命名，两边不会错位。 */
#define VOICE_DIR   "data/voice/"
#define VOICE_EXT   ".ogg"
#define VOICE_EXT2  ".wav"       /* 没有 ffmpeg 时 gen_voice.py 留 wav */

#define VOICE_SLOTS 8            /* 缓存几句：来回读档/回看时不用反复解码 */
#define VOICE_PATH  160

typedef struct {
    char  path[VOICE_PATH];
    Sound snd;
    int   ok;
    unsigned int used;           /* 最近一次使用的时间戳，满了就换最旧的 */
} Slot;

static Slot  g_slots[VOICE_SLOTS];
static unsigned int g_clock;

static int g_device;             /* 音频设备是否可用 */
static int g_enabled = 1;
static int g_cur = -1;           /* 正在播的槽位；-1 = 没在播 */
static float g_volume = 0.9f;

static int g_loaded;             /* 成功加载过的不同文件数，自检用 */

/* 节点 id 直接来自剧本，这里只做基本的文件名净化（不允许它跑出 data/voice/）。 */
static void safe_component(const char *src, char *dst, int size)
{
    int j = 0;
    for (int i = 0; src[i] && j < size - 1; i++) {
        char c = src[i];
        int bad = (c == '/' || c == '\\' || c == ':' || c == '*' ||
                   c == '?' || c == '"' || c == '<' || c == '>' || c == '|');
        dst[j++] = bad ? '_' : c;
    }
    dst[j] = '\0';
}

/* 拼出这一句可能的文件路径。返回能用的那个，都没有返回 0。 */
static int line_paths(const char *node_id, int line_index, char *ogg, char *wav, int size)
{
    char id[64];
    safe_component(node_id, id, (int)sizeof(id));

    snprintf(ogg, size, "%s%s_%d%s", VOICE_DIR, id, line_index, VOICE_EXT);
    snprintf(wav, size, "%s%s_%d%s", VOICE_DIR, id, line_index, VOICE_EXT2);

    if (FileExists(ogg)) return 1;
    if (FileExists(wav)) return 2;
    return 0;
}

static int slot_find(const char *path)
{
    for (int i = 0; i < VOICE_SLOTS; i++)
        if (g_slots[i].ok && strcmp(g_slots[i].path, path) == 0) return i;
    return -1;
}

/* 找一个空槽；都满了就挑最久没用的那个（先停掉，再卸）。 */
static int slot_alloc(void)
{
    for (int i = 0; i < VOICE_SLOTS; i++)
        if (!g_slots[i].ok) return i;

    int oldest = 0;
    for (int i = 1; i < VOICE_SLOTS; i++)
        if (g_slots[i].used < g_slots[oldest].used) oldest = i;

    if (g_cur == oldest) { StopSound(g_slots[oldest].snd); g_cur = -1; }
    UnloadSound(g_slots[oldest].snd);
    memset(&g_slots[oldest], 0, sizeof(g_slots[oldest]));
    return oldest;
}

static int slot_load(const char *path)
{
    int idx = slot_find(path);
    if (idx >= 0) {
        g_slots[idx].used = ++g_clock;
        return idx;
    }

    idx = slot_alloc();
    Sound snd = LoadSound(path);
    if (!IsSoundValid(snd)) {
        memset(&g_slots[idx], 0, sizeof(g_slots[idx]));
        return -1;
    }
    SetSoundVolume(snd, g_volume);

    snprintf(g_slots[idx].path, sizeof(g_slots[idx].path), "%s", path);
    g_slots[idx].snd  = snd;
    g_slots[idx].ok   = 1;
    g_slots[idx].used = ++g_clock;
    g_loaded++;
    return idx;
}

void voice_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_device = 0;
    g_cur = -1;
    g_clock = 0;
    g_loaded = 0;
    g_enabled = 1;

    const char *env = getenv("GAL_VOICE");
    if (env && (env[0] == '0' || env[0] == 'n' || env[0] == 'N')) {
        g_enabled = 0;
        printf("配音已按 GAL_VOICE=%s 关闭\n", env);
    }

    if (!IsAudioDeviceReady()) {
        printf("没有可用的音频设备，台词配音静音运行\n");
        return;
    }
    g_device = 1;

    if (FileExists(VOICE_DIR)) {
        int n = 0;
        FilePathList list = LoadDirectoryFiles(VOICE_DIR);
        for (unsigned int i = 0; i < list.count; i++) {
            const char *ext = GetFileExtension(list.paths[i]);
            if (ext && (strcmp(ext, ".ogg") == 0 || strcmp(ext, ".wav") == 0)) n++;
        }
        UnloadDirectoryFiles(list);
        printf("配音：%s 里找到 %d 句%s\n", VOICE_DIR, n,
               n ? "" : "（先生成：python tools/gen_voice.py）");
    } else {
        printf("配音：%s 不存在，静音运行（python tools/gen_voice.py 生成）\n", VOICE_DIR);
    }
}

void voice_shutdown(void)
{
    if (!g_device) return;

    for (int i = 0; i < VOICE_SLOTS; i++) {
        if (!g_slots[i].ok) continue;
        UnloadSound(g_slots[i].snd);
        memset(&g_slots[i], 0, sizeof(g_slots[i]));
    }
    g_cur = -1;
    g_device = 0;
}

void voice_update(void)
{
    if (!g_device || g_cur < 0) return;

    /* 播完就把槽位标记成“空闲播放位”，但缓存保留：这一句以后可能还要听 */
    if (!IsSoundPlaying(g_slots[g_cur].snd)) g_cur = -1;
}

void voice_stop(void)
{
    if (!g_device || g_cur < 0) return;
    StopSound(g_slots[g_cur].snd);
    g_cur = -1;
}

void voice_play_line(const char *node_id, int line_index, const char *speaker)
{
    if (!g_device || !g_enabled) return;
    if (!node_id || !node_id[0]) return;

    /* 旁白没有配音：剧本里 speaker 为空的行不念，只有人物台词才出声 */
    if (!speaker || !speaker[0]) return;

    voice_stop();

    char ogg[VOICE_PATH], wav[VOICE_PATH];
    if (!line_paths(node_id, line_index, ogg, wav, VOICE_PATH)) return;  /* 没配 = 静默 */

    const char *path = FileExists(ogg) ? ogg : wav;
    int idx = slot_load(path);
    if (idx < 0) return;

    SetSoundVolume(g_slots[idx].snd, g_volume);
    PlaySound(g_slots[idx].snd);
    g_cur = idx;
}

void voice_set_enabled(int on)
{
    if (!on) voice_stop();
    g_enabled = on ? 1 : 0;
}

int voice_enabled(void)
{
    return g_enabled;
}

void voice_set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_volume = v;

    for (int i = 0; i < VOICE_SLOTS; i++)
        if (g_slots[i].ok) SetSoundVolume(g_slots[i].snd, g_volume);
}

float voice_volume(void)
{
    return g_volume;
}

int voice_is_playing(void)
{
    if (!g_device || g_cur < 0) return 0;
    return IsSoundPlaying(g_slots[g_cur].snd) ? 1 : 0;
}

int voice_loaded_count(void)
{
    return g_loaded;
}
