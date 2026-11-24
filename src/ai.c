#include "ai.h"
#include "game.h"
#include "contract.h"

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

/* 提示词模板的版本号。改了下面的模板就要 +1，否则同一分支还会命中旧缓存，
   让人以为"改了没生效"。
   3: 事件库按好感裁剪、max_tokens、输出收窄到 2-4 行
   4: 状态栏简化成「【判定：+2】」，输出 2-3 行 */
#define PROMPT_VERSION  7

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
static char     g_partial[AI_REPLY_MAX];   /* 流式过程中已收到的部分 */
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

int ai_partial_copy(char *out, int size)
{
    if (!out || size <= 1) return 0;
    out[0] = '\0';

    pthread_mutex_lock(&g_lock);
    int n = (int)strlen(g_partial);
    if (n >= size) n = size - 1;
    if (n > 0) memcpy(out, g_partial, (size_t)n);
    out[n] = '\0';
    pthread_mutex_unlock(&g_lock);

    return n;
}

const char *ai_error(void)
{
    return g_error[0] ? g_error : "";
}

void ai_set_config(const char *api_key, const char *model)
{
    if (api_key) snprintf(g_key, sizeof(g_key), "%s", api_key);
    if (model)   snprintf(g_model, sizeof(g_model), "%s", model);
}

const char *ai_get_key(void) { return g_key; }

void ai_save_config(const char *api_key, const char *model)
{
    if (api_key) snprintf(g_key, sizeof(g_key), "%s", api_key);
    if (model)   snprintf(g_model, sizeof(g_model), "%s", model);

    FILE *f = fopen("data/ai.conf", "w");
    if (!f) return;
    fprintf(f, "# 由游戏内「API Key 设置」写入，改完不用重新编译。\n");
    fprintf(f, "# 不填 key 也能玩：走角色卡离线台词，流程完整，不会报错。\n");
    fprintf(f, "key=%s\n", g_key);
    fprintf(f, "model=%s\n", g_model);
    fclose(f);
}

/* ---------------- 事件库裁剪 ---------------- */

/* 从 "好感5-15" / "好感≥46" / "好感0" 里抠出区间。
   抠不出来（H 段写「第N天」）返回 0。 */
static int parse_aff_range(const char *line, int *lo, int *hi)
{
    const char *p = strstr(line, "好感");
    if (!p) return 0;
    p += 6;                                   /* "好感" 是 6 字节 */

    while (*p == ' ') p++;

    int ge = 0;
    if (strncmp(p, "≥", 3) == 0) { p += 3; ge = 1; }
    else if (*p == '>') { p++; if (*p == '=') p++; ge = 1; }

    if (*p < '0' || *p > '9') return 0;

    int a = atoi(p);
    while (*p >= '0' && *p <= '9') p++;

    /* 区间写法 "5-15" */
    if (*p == '-' || *p == '~') {
        p++;
        if (*p >= '0' && *p <= '9') {
            *lo = a;
            *hi = atoi(p);
            if (*hi < *lo) return 0;
            return 1;
        }
    }

    *lo = a;
    *hi = ge ? 100 : a;
    return 1;
}

/* 这一条是不是 H 段（D4 表白线之后，写的是「D4之后第1天」而不是好感区间）。
 *
 * 注意 character.c 解析角色卡时已经把行首的 "events " 去掉了，
 * 所以每行实际是「A1 连续同坐第三天｜好感5-15：……」或「H1 立起来的书｜D4之后第1天：……」。
 * 按事件编号的首字母判断，不能只搜「第」——「连续同坐第三天」里也有「第」。
 */
static int is_h_event(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    return p[0] == 'H' && p[1] >= '1' && p[1] <= '9';
}

int ai_trim_events(const char *events, int aff, int stage, char *out, int out_size)
{
    if (!out || out_size <= 0) return 0;
    out[0] = '\0';
    if (!events || !*events) return 0;

    const int lo_need = aff - 4;      /* 刚演过的一两条，模型可能还要引用 */
    const int hi_need = aff + 12;     /* 马上会遇到的，给它一个方向 */

    int used = 0;
    const char *p = events;

    while (*p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);

        int keep = 0;
        int lo = 0, hi = 0;

        if (parse_aff_range(p, &lo, &hi)) {
            /* 区间和 [lo_need, hi_need] 有交集就留 */
            if (lo <= hi_need && hi >= lo_need) keep = 1;
        } else if (is_h_event(p)) {
            /* H1–H9：只在表白线之后有用 */
            if (stage >= 4) keep = 1;
        } else {
            /* 认不出格式的条目一律保留，宁可多传也别把规则丢掉 */
            keep = 1;
        }

        if (keep && len > 0) {
            if (used + len + 2 >= out_size) break;
            memcpy(out + used, p, (size_t)len);
            used += len;
            out[used++] = '\n';
            out[used] = '\0';
        }

        if (!nl) break;
        p = nl + 1;
    }

    return used;
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
    /* 事件库按当前好感裁一遍：46 条 11KB 全传，模型每轮都要重新读一遍，
       首字延迟和费用都翻几倍，而里面九成条目这一档根本不会触发。 */
    static char events_trimmed[AI_EVENTS_MAX];
    ai_trim_events(p->events, p->affection, p->stage_index,
                   events_trimmed, (int)sizeof(events_trimmed));

    /* 提示词模板：人设 + 状态 + 事件库 + 续写指令 + 输出格式。
       字段全部来自角色卡和剧本，改内容不用动代码。 */
    char system_plain[AI_SYSTEM_PLAIN];
    snprintf(system_plain, sizeof(system_plain),
             "你扮演视觉小说女主「%s」。\n"
             "%s\n\n"
             "【状态】好感 %d/100｜阶段 %s｜地点 %s｜最近事件 %s\n"
             "【来龙去脉】%s\n"
             "【本场场景】%s\n"
             "【已知信息】%s\n"
             "【称呼】她叫你「%s」，你叫她「%s」\n\n"
             "【规则】\n%s\n\n"
             "【当前阶段语气】%s\n\n"
             "【事件库·按触发条件与结算判定，不许自创事件】\n%s\n\n"
             "【对不上事件库的时候】玩家随口说的话往往不落在上面任何一条里，"
             "这时按这个基准判，别凭感觉：\n"
             "  +1  具体的帮忙或关心，而且没越界（记得她说过的事、顺手做了件实事）\n"
             "   0  普通搭话、客套、没有信息量的寒暄\n"
             "  -1  冒犯、越界、打探隐私、把她当谈资\n"
             "  同一个人反复说同类的话，第二次起要减到 0——她不重复领情。\n\n"
             "【怎么写】\n"
             "1. 一行只放一种东西：台词行只写她说出口的话（不加引号不加动作），动作神态另起一行「旁白：」。\n"
             "2. 一共 2 到 3 行，越短越好；阶段 2 之后最多 3 句台词。\n"
             "3. 心里对照上面的「结算」定好感增减，只把结果写进最后一行判定，正文不写数字不写理由。\n"
             "4. 不替玩家行动，不输出选项，不写心理独白。\n\n"
             "【输出示例】\n"
             "%s：……你查了哪本。\n"
             "旁白：她笔停了一下，抬眼看过来。\n"
             "【判定：+2】\n\n"
             "最后一行必须只有判定，格式固定「【判定：+2】」，不写别的字段（好感/阶段/记忆引擎自己都有）。\n"
             "判定只写带正负号的整数（+1 / -3 / 0），通常 ±1~3，对照事件库「结算」来定，不凭感觉。"
             "缺这一行这次回答就算没写完。",
             p->character_name[0] ? p->character_name : "她",
             p->persona,
             p->affection,
             p->stage,
             p->location[0] ? p->location : "省图三楼靠窗",
             p->last_event[0] ? p->last_event : "（无）",
             p->history[0] ? p->history : "（还没有共同经历）",
             p->scene,
             p->memory[0] ? p->memory : "（刚认识）",
             p->player_call[0] ? p->player_call : "你",
             p->her_call[0] ? p->her_call : "你",
             p->rules,
             p->stage_tone,
             events_trimmed,
             p->character_name[0] ? p->character_name : "她");

    char sys_esc[AI_SYSTEM_ESC];
    char user_esc[AI_USER_ESC];
    json_escape(system_plain, sys_esc, (int)sizeof(sys_esc));
    json_escape(p->user_input, user_esc, (int)sizeof(user_esc));

    /* temperature 0.9：1.0 时她偶尔会把同一条规则绕两遍，首字一样慢但要多想。
       max_tokens 420：2~4 行台词加状态栏撑不满，设上限能让服务端早点收尾。
       presence/frequency 惩罚压低：角色台词本来就短，重复惩罚反而是噪音。 */
    return snprintf(out, (size_t)out_size,
        "{\"model\":\"%s\",\"stream\":true,\"temperature\":0.85,\"max_tokens\":420,"
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
    /* 缓存键包含人设、场景、阶段、玩家台词、好感，外加提示词模板的版本号。
       前几项改了会重新生成；模板改了要手动把 PROMPT_VERSION 加一，
       否则同一分支还会命中旧缓存，看起来像"改了没生效"。 */
    char key[8192];
    snprintf(key, sizeof(key), "v%d|%s|%s|%s|%s|%s|%d",
             PROMPT_VERSION, p->character_id, p->persona, p->scene, p->stage,
             p->user_input, p->affection);
    snprintf(out, (size_t)size, CACHE_DIR "/%08x.txt", fnv1a(key));
}

/* ---------------- 请求 ---------------- */

/* 网络超时。以前是 90 秒——网卡一下就是一分半钟定在「生成中」，
   玩家只会以为游戏死了。20 秒足够（实测一轮 1 秒），超了就按失败处理。
   主循环那边还有一层客户端超时兜底，见 g_ai_wait。 */
#define AI_CURL_TIMEOUT 20
#define AI_STR_(x) #x
#define AI_STR(x)  AI_STR_(x)

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
        "curl -s -N --max-time " AI_STR(AI_CURL_TIMEOUT) " -X POST \"%s\" "
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
            if (used + (size_t)n < sizeof(reply) - 1) {
                strcat(reply, delta);

                /* 每收到一段就更新一份快照，UI 那边逐帧拷过去显示。
                   加锁是因为主线程随时可能来读；临界区只有一次 strcpy，
                   比等整段生成完再显示快得多。 */
                pthread_mutex_lock(&g_lock);
                snprintf(g_partial, sizeof(g_partial), "%s", reply);
                pthread_mutex_unlock(&g_lock);
            }
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

        /* 成功的生成结果写缓存，下次同一输入直接复用。
           自由输入不写——每句话都该是新的，写进去只会污染缓存。 */
        if (!g_job.no_cache) {
            char path[160];
            cache_path(&g_job, path, (int)sizeof(path));
            FILE *cf = fopen(path, "wb");
            if (cf) {
                fwrite(g_reply, 1, strlen(g_reply), cf);
                fclose(cf);
            }
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
    g_partial[0] = '\0';
    g_error[0] = '\0';
    g_source   = 0;

    /* 1. 先查缓存：同一份 prompt 生成过就直接复用。
          自由输入（no_cache）跳过这一步——否则同一句话永远得到同样的回答。 */
    char path[160];
    if (!p->no_cache) {
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

void ai_abandon(void)
{
    /* 主循环已经决定不等了：清掉流式残留，界面立刻恢复正常。
       后台线程继续跑到自己的 20 秒超时为止，结果会被丢弃——
       因为调用方把 g_ai_pending 清零了，不会再取。
       在此期间发起新请求会被 ai_request 拒掉（state 还是 BUSY），
       调用方会提示「稍等」，不会出现两个线程同时写结果。 */
    pthread_mutex_lock(&g_lock);
    g_partial[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}
