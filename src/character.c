#include "character.h"
#include "contract.h"

#include <stdio.h>
#include <string.h>

/* 角色卡解析。
 *
 * 一行一个字段：`键 值`，也接受 `键=值`。
 * persona / rules / stageN / offlineN 写多行会自动用换行拼起来，
 * 这样人设可以像写文档一样分段，不用把整段塞在一行里。
 */

static const char *STAGE_LABEL[5] = {
    "0 陌生书友",
    "1 脸熟自习搭子",
    "2 资料互助",
    "3 在意",
    "4 心动"
};

static CharacterCard g_cards[MAX_CARDS];
static int           g_card_count;

static CharacterCard g_default;
static int           g_default_ready;

/* ---------------- 小工具 ---------------- */

static int u8_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x06) return 2;
    if ((c >> 4) == 0x0E) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

static void trim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';

    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

/* 按码点追加，不会把一个汉字切一半。dst 满就静默截断。 */
static void append_utf8(char *dst, int size, const char *src)
{
    int used = (int)strlen(dst);

    while (*src && used < size - 1) {
        int len = u8_len((unsigned char)*src);
        if (used + len >= size) break;
        memcpy(dst + used, src, (size_t)len);
        used += len;
        src += len;
    }
    dst[used] = '\0';
}

/* 多行字段之间用换行连接；name / 称呼这类单行字段用空格。 */
static void field_append(char *dst, int size, const char *val, int multiline)
{
    if (!val || !*val) return;

    if (dst[0]) {
        int used = (int)strlen(dst);
        if (used < size - 2) {
            if (multiline) { dst[used++] = '\n'; }
            else           { dst[used++] = ' ';  }
            dst[used] = '\0';
        }
    }
    append_utf8(dst, size, val);
}

/* 把 `键 值` 拆开，返回键长度并让 *val 指向值。 */
static int split_kv(char *line, char **val)
{
    char *p = line;
    while (*p && *p != ' ' && *p != '\t' && *p != '=') p++;
    if (*p) { *p = '\0'; *val = p + 1; } else { *val = p; }

    trim(*val);
    return (int)strlen(line);
}

static int key_is(const char *key, const char *alias)
{
    return strcmp(key, alias) == 0;
}

/* ---------------- 解析 ---------------- */

static void apply_field(CharacterCard *c, const char *key, const char *val)
{
    if (key_is(key, "name") || key_is(key, "名字")) {
        field_append(c->name, (int)sizeof(c->name), val, 0);
    } else if (key_is(key, "persona") || key_is(key, "人设")) {
        field_append(c->persona, (int)sizeof(c->persona), val, 1);
    } else if (key_is(key, "rules") || key_is(key, "规则")) {
        field_append(c->rules, (int)sizeof(c->rules), val, 1);
    } else if (key_is(key, "events") || key_is(key, "事件")) {
        field_append(c->events, (int)sizeof(c->events), val, 1);
    } else if (key_is(key, "scene") || key_is(key, "场景")) {
        field_append(c->scene, (int)sizeof(c->scene), val, 1);
    } else if (key_is(key, "line") || key_is(key, "台词")) {
        field_append(c->line, (int)sizeof(c->line), val, 1);
    } else if (key_is(key, "player_call")) {
        field_append(c->player_call, (int)sizeof(c->player_call), val, 0);
    } else if (key_is(key, "her_call")) {
        field_append(c->her_call, (int)sizeof(c->her_call), val, 0);
    } else if (strncmp(key, "stage", 5) == 0 && key[5] >= '0' && key[5] <= '4' && key[6] == '\0') {
        int i = key[5] - '0';
        field_append(c->stage_tone[i], (int)sizeof(c->stage_tone[i]), val, 1);
    } else if (strncmp(key, "offline", 7) == 0 && key[7] >= '0' && key[7] <= '4' && key[8] == '\0') {
        int i = key[7] - '0';
        field_append(c->offline[i], (int)sizeof(c->offline[i]), val, 1);
    }
    /* 未知键静默忽略，方便以后加字段 */
}

int characters_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("角色卡未找到（%s），使用内置默认人设\n", path);
        return 0;
    }

    char buf[2048];
    CharacterCard *cur = NULL;
    int first = 1;
    int line_no = 0;

    while (fgets(buf, (int)sizeof(buf), f)) {
        char *line = buf;
        line_no++;

        if (first) {
            unsigned char *u = (unsigned char *)line;
            if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) line += 3;
            first = 0;
        }

        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        if (line[0] == ':' && line[1] == ':') {
            if (g_card_count >= MAX_CARDS) {
                fprintf(stderr, "角色卡超过 %d 张，%s:%d 之后被忽略\n", MAX_CARDS, path, line_no);
                break;
            }
            cur = &g_cards[g_card_count++];
            memset(cur, 0, sizeof(*cur));
            trim(line + 2);
            append_utf8(cur->id, (int)sizeof(cur->id), line + 2);
            if (!cur->id[0]) cur = NULL;
            continue;
        }

        if (!cur) continue;

        char *val = NULL;
        int   klen = split_kv(line, &val);
        if (klen == 0) continue;
        apply_field(cur, line, val);
    }

    fclose(f);

    /* 没写名字的卡用 id 顶上，免得提示词里出现空名字 */
    int usable = 0;
    for (int i = 0; i < g_card_count; i++) {
        if (!g_cards[i].name[0])
            append_utf8(g_cards[i].name, (int)sizeof(g_cards[i].name), g_cards[i].id);
        if (g_cards[i].id[0]) usable++;
    }
    return usable;
}

int characters_count(void) { return g_card_count; }

const CharacterCard *character_find(const char *id)
{
    if (!id || !*id) return NULL;
    for (int i = 0; i < g_card_count; i++)
        if (strcmp(g_cards[i].id, id) == 0) return &g_cards[i];
    return NULL;
}

int character_stage_of(int affection)
{
    if (affection <= 20) return 0;
    if (affection <= 45) return 1;
    if (affection <= 70) return 2;
    if (affection <= 85) return 3;
    return 4;
}

const char *character_stage_name(int stage)
{
    if (stage < 0) stage = 0;
    if (stage > 4) stage = 4;
    return STAGE_LABEL[stage];
}

const CharacterCard *character_default(void)
{
    if (g_default_ready) return &g_default;

    memset(&g_default, 0, sizeof(g_default));

    append_utf8(g_default.id, (int)sizeof(g_default.id), "shenzhiji");
    append_utf8(g_default.name, (int)sizeof(g_default.name), "沈知霁");
    append_utf8(g_default.persona, (int)sizeof(g_default.persona),
        "二十一岁，成都某高校历史系大三学生，方向偏巴蜀地方史与古典文献，准备保研。"
        "常去四川省图书馆三楼社科区靠窗座位。清冷、专注、礼貌、慢热，边界感强。"
        "说话短句，少语气词，不主动延伸话题；拒绝时不伤人但很清楚。");
    append_utf8(g_default.rules, (int)sizeof(g_default.rules),
        "严格遵守当前好感阶段，不提前暧昧、不吃醋、不表白、不主动肢体接触。");
    append_utf8(g_default.scene, (int)sizeof(g_default.scene),
        "成都秋雨天，四川省图书馆三楼社科区靠窗座位，馆内很安静。");
    append_utf8(g_default.line, (int)sizeof(g_default.line),
        "玩家坐到对面，把伞收在脚边。");
    append_utf8(g_default.player_call, (int)sizeof(g_default.player_call), "你");
    append_utf8(g_default.her_call, (int)sizeof(g_default.her_call), "你");

    const char *tones[5] = {
        "公事公办，礼貌疏离，不延伸话题。",
        "记得玩家习惯，偶尔多说半句。",
        "愿意解释专业、分享资料，接受小帮忙但会回礼。",
        "会等玩家出现，关心藏在细节里，嘴硬。",
        "只对玩家有轻微例外，但会回避直接表白。"
    };
    for (int i = 0; i < 5; i++)
        append_utf8(g_default.stage_tone[i], (int)sizeof(g_default.stage_tone[i]), tones[i]);

    g_default_ready = 1;
    return &g_default;
}