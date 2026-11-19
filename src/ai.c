#include "ai.h"
#include "game.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#if defined(_WIN32)
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p) mkdir(p, 0755)
#endif

#define API_URL   "https://api.deepseek.com/chat/completions"
#define CACHE_DIR "data/cache"

/* 拼好的 system prompt 与 JSON 转义缓冲。
   人设卡可以写到 1600 字节，转义后最坏情况按 4 倍留。 */
#define AI_SYSTEM_PLAIN 24000
#define AI_SYSTEM_ESC   56000
#define AI_USER_ESC      2400
#define AI_BODY_SIZE    64000

/* 角色卡没写 offlineN 时的最后兜底。故意不带人名，
   只做“她没抬头、馆里很安静”这类中性动作，任何角色都能用。 */
static const char *OFFLINE_FALLBACK =
    "旁白：她把手里的书往自己那边收了收，没有抬头。\n"
    "旁白：馆里很安静，只有翻页声和笔尖摩擦纸面的声音。\n"
    "旁白：窗外的雨还没停。";

static char g_key[160];
static char g_model[64] = "deepseek-chat";

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int      g_state = AI_IDLE;
static char     g_reply[AI_REPLY_MAX];
static char     g_error[160];
static int      g_source;          /* 0=真实生成 1=缓存 2=离线兜底 */
static pthread_t g_thread;
static int      g_thread_running;
static AiPrompt g_job;

/* ---------------- 配置 ---------------- */

static void load_config(void)
{
    const char *env = getenv("DEEPSEEK_API_KEY");
    if (env && *env) {
        snprintf(g_key, sizeof(g_key), "%s", env);
        return;
    }

    FILE *f = fopen("data/ai.conf", "r");
    if (!f) return;

    char line[320];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char *k = line;
        char *v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        while (*v == ' ' || *v == '\t') v++;

        char *end = v + strlen(v);
        while (end > v && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = '\0';

        if (strcmp(k, "key") == 0)        snprintf(g_key, sizeof(g_key), "%s", v);
        else if (strcmp(k, "model") == 0) snprintf(g_model, sizeof(g_model), "%s", v);
    }
    fclose(f);
}

void ai_init(void)
{
    MAKE_DIR("data");
    MAKE_DIR(CACHE_DIR);
    load_config();
    g_state = AI_IDLE;
}

void ai_shutdown(void)
{
    if (g_thread_running) {
        pthread_join(g_thread, NULL);
        g_thread_running = 0;
    }
}

int  ai_is_configured(void) { return g_key[0] != '\0'; }
int  ai_state(void)         { return g_state; }
int  ai_from_cache(void)    { return g_source; }
const char *ai_reply(void)  { return g_reply; }

const char *ai_error(void)
{
    return g_error[0] ? g_error : "";
}

void ai_set_config(const char *api_key, const char *model)
{
    if (api_key) snprintf(g_key, sizeof(g_key), "%s", api_key);
    if (model)   snprintf(g_model, sizeof(g_model), "%s", model);
}

/* ---------------- JSON ---------------- */

static void json_escape(const char *s, char *out, int size)
{
    int i = 0;
    if (size <= 0) return;

    while (*s && i < size - 8) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  out[i++] = '\\'; out[i++] = '"';  break;
            case '\\': out[i++] = '\\'; out[i++] = '\\'; break;
            case '\n': out[i++] = '\\'; out[i++] = 'n';  break;
            case '\r': out[i++] = '\\'; out[i++] = 'r';  break;
            case '\t': out[i++] = '\\'; out[i++] = 't';  break;
            default:
                if (c < 0x20) i += snprintf(out + i, (size_t)(size - i), "\\u%04x", c);
                else          out[i++] = (char)c;
        }
        s++;
    }
    out[i] = '\0';
}

int ai_build_request_json(const AiPrompt *p, char *out, int out_size)
{
    /* 提示词模板：人设 + 状态 + 事件库 + 续写指令 + 输出格式。
       字段全部来自角色卡和剧本，改内容不用动代码。 */
    char system_plain[AI_SYSTEM_PLAIN];
    snprintf(system_plain, sizeof(system_plain),
             "你扮演视觉小说女主「%s」。\n"
             "%s\n\n"
             "当前好感度：%d / 100\n"
             "当前阶段：%s\n"
             "当前地点：%s\n"
             "最近事件：%s\n"
             "已经发生的事：%s\n"
             "当前场景：%s\n"
             "已知信息：%s\n"
             "玩家称呼你：%s\n"
             "你称呼玩家：%s\n\n"
             "行为规则：\n%s\n\n"
             "当前阶段语气：%s\n\n"
             "事件库（只按这些事件的触发条件与结算判定，不许自创事件）：\n%s\n\n"
             "本轮续写指令：\n"
             "1. 先判定玩家行为类型：守规矩 / 越界 / 中性 / 小帮忙 / 资料相关 / 暧昧试探 / 表白。\n"
             "2. 对照阶段门槛和事件库，判定好感增减（只按规则表，不凭感觉）。\n"
             "3. 用「动作/神态 + 1-2 句台词」写%s的反应；阶段 2 之后可到 3 句。\n"
             "4. 若行为触发阶段晋升条件，写一个她态度微变的细节（多看你一眼、多说半句、把杯子挪近半寸），但不点破。\n"
             "5. 不替玩家行动，不输出选项，不写心理独白。\n"
             "6. 每行严格写成「%s：内容」或「旁白：内容」；一共 3 到 5 行。\n"
             "7. 最后单独一行输出状态栏，格式固定：\n"
             "   【好感：x/100｜阶段：x｜记忆：新增信息｜判定：+x/-x，原因】\n"
             "   这一行引擎会隐藏，玩家看不到，但必须输出。",
             p->character_name[0] ? p->character_name : "她",
             p->persona,
             p->affection,
             p->stage,
             p->location[0] ? p->location : "省图三楼靠窗",
             p->last_event[0] ? p->last_event : "（无，接着当前场景）",
             p->history[0] ? p->history : "（还没有共同经历）",
             p->scene,
             p->memory[0] ? p->memory : "（刚认识，还没有共同经历）",
             p->player_call[0] ? p->player_call : "你",
             p->her_call[0] ? p->her_call : "你",
             p->rules,
             p->stage_tone,
             p->events,
             p->character_name[0] ? p->character_name : "她",
             p->character_name[0] ? p->character_name : "她");

    char sys_esc[AI_SYSTEM_ESC];
    char user_esc[AI_USER_ESC];
    json_escape(system_plain, sys_esc, (int)sizeof(sys_esc));
    json_escape(p->user_input, user_esc, (int)sizeof(user_esc));

    return snprintf(out, (size_t)out_size,
        "{\"model\":\"%s\",\"stream\":true,\"temperature\":1.0,"
        "\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}]}",
        g_model, sys_esc, user_esc);
}

/* ---------------- SSE ---------------- */

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int encode_utf8(unsigned int cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

/* 解析一行 SSE。
   返回 >0 表示取到增量文本，0 表示这行没有内容，-1 表示流结束。 */
int ai_parse_sse_line(const char *line, char *delta, int delta_size)
{
    delta[0] = '\0';
    if (!line || delta_size < 2) return 0;

    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, "data:", 5) != 0) return 0;

    line += 5;
    while (*line == ' ') line++;

    if (strncmp(line, "[DONE]", 6) == 0) return -1;

    const char *p = strstr(line, "\"content\":");
    if (!p) return 0;
    p += 10;
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;

    int i = 0;
    while (*p && *p != '"' && i < delta_size - 4) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n':  delta[i++] = '\n'; p++; break;
                case 't':  delta[i++] = '\t'; p++; break;
                case 'r':  delta[i++] = '\r'; p++; break;
                case '"':  delta[i++] = '"';  p++; break;
                case '\\': delta[i++] = '\\'; p++; break;
                case 'u': {
                    unsigned int cp = 0;
                    p++;
                    for (int k = 0; k < 4 && *p; k++) {
                        cp = cp * 16u + (unsigned int)hex_val(*p);
                        p++;
                    }
                    i += encode_utf8(cp, delta + i);
                    break;
                }
                default: delta[i++] = *p++; break;
            }
        } else {
            delta[i++] = *p++;
        }
    }
    delta[i] = '\0';
    return i;
}

/* ---------------- 缓存 ---------------- */

static unsigned int fnv1a(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static void cache_path(const AiPrompt *p, char *out, int size)
{
    /* 缓存键包含人设、场景、阶段、玩家台词：改任何一项都会重新生成，
       不会出现“人设改了但还在读旧缓存”的错觉。 */
    char key[8192];
    snprintf(key, sizeof(key), "%s|%s|%s|%s|%s|%d",
             p->character_id, p->persona, p->scene, p->stage,
             p->user_input, p->affection);
    snprintf(out, (size_t)size, CACHE_DIR "/%08x.txt", fnv1a(key));
}

/* ---------------- 请求 ---------------- */

static void do_request(void)
{
    char body[AI_BODY_SIZE];
    ai_build_request_json(&g_job, body, (int)sizeof(body));

    FILE *f = fopen(CACHE_DIR "/_req.json", "wb");
    if (!f) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_error, sizeof(g_error), "无法写入临时请求文件");
        g_state = AI_FAILED;
        pthread_mutex_unlock(&g_lock);
        return;
    }
    fwrite(body, 1, strlen(body), f);
    fclose(f);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -s -N --max-time 90 -X POST \"%s\" "
        "-H \"Content-Type: application/json\" "
        "-H \"Authorization: Bearer %s\" "
        "-d @%s/_req.json 2>nul",
        API_URL, g_key, CACHE_DIR);

    FILE *pipe = _popen(cmd, "r");
    if (!pipe) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_error, sizeof(g_error), "无法启动 curl 子进程");
        g_state = AI_FAILED;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    char line[3072];
    int  got_error = 0;
    char reply[AI_REPLY_MAX];
    reply[0] = '\0';

    while (fgets(line, sizeof(line), pipe)) {
        char delta[AI_DELTA_MAX];
        int n = ai_parse_sse_line(line, delta, (int)sizeof(delta));
        if (n > 0) {
            size_t used = strlen(reply);
            if (used + (size_t)n < sizeof(reply) - 1)
                strcat(reply, delta);
        } else if (n == 0 && strstr(line, "\"error\"")) {
            got_error = 1;
            char *m = strstr(line, "\"message\"");
            snprintf(g_error, sizeof(g_error), "%s",
                     m ? "服务端返回错误" : "请求失败");
        }
    }
    _pclose(pipe);

    pthread_mutex_lock(&g_lock);
    if (reply[0]) {
        snprintf(g_reply, sizeof(g_reply), "%s", reply);
        g_source = 0;
        g_state  = AI_DONE;

        /* 成功的生成结果写缓存，下次同一输入直接复用 */
        char path[160];
        cache_path(&g_job, path, (int)sizeof(path));
        FILE *cf = fopen(path, "wb");
        if (cf) {
            fwrite(g_reply, 1, strlen(g_reply), cf);
            fclose(cf);
        }
    } else {
        if (!g_error[0])
            snprintf(g_error, sizeof(g_error),
                     got_error ? "服务端返回错误" : "未收到有效内容");
        g_state = AI_FAILED;
    }
    pthread_mutex_unlock(&g_lock);
}

static void *worker(void *arg)
{
    (void)arg;
    do_request();
    return NULL;
}

int ai_request(const AiPrompt *p)
{
    pthread_mutex_lock(&g_lock);

    if (g_state == AI_BUSY) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    memcpy(&g_job, p, sizeof(AiPrompt));
    g_reply[0] = '\0';
    g_error[0] = '\0';
    g_source   = 0;

    /* 1. 先查缓存：同一份 prompt 生成过就直接复用 */
    char path[160];
    cache_path(p, path, (int)sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(g_reply, 1, sizeof(g_reply) - 1, f);
        fclose(f);
        if (n > 0) {
            g_reply[n] = '\0';
            g_source = 1;
            g_state  = AI_DONE;
            pthread_mutex_unlock(&g_lock);
            return 1;
        }
    }

    /* 2. 没配置 key：走离线兜底，流程照样能演示。
          优先用角色卡里按阶段写好的 offlineN 台词。 */
    if (!g_key[0]) {
        snprintf(g_reply, sizeof(g_reply), "%s",
                 p->offline[0] ? p->offline : OFFLINE_FALLBACK);
        g_source = 2;
        g_state  = AI_DONE;
        pthread_mutex_unlock(&g_lock);
        return 1;
    }

    /* 3. 真实请求，丢到后台线程 */
    g_state = AI_BUSY;
    if (pthread_create(&g_thread, NULL, worker, NULL) != 0) {
        snprintf(g_error, sizeof(g_error), "无法创建请求线程");
        g_state = AI_FAILED;
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    g_thread_running = 1;
    pthread_mutex_unlock(&g_lock);
    return 1;
}

void ai_poll(void)
{
    if (!g_thread_running) return;

    pthread_mutex_lock(&g_lock);
    int finished = (g_state != AI_BUSY);
    pthread_mutex_unlock(&g_lock);

    if (finished) {
        pthread_join(g_thread, NULL);
        g_thread_running = 0;
    }
}
