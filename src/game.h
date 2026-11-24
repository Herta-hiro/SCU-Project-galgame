#ifndef GAME_H
#define GAME_H

/* 剧本与运行时状态的核心数据结构。
   设计原则：状态全部集中在一个 Game 结构体里，
   存档 = 把这个结构体的可持久化部分序列化，不做增量。 */

/* 节点上限。原来 96，加到 95 个就顶死了，加一个事件都要先改这个宏。
   160 留出一倍余量：一个事件节点约 10KB（16 行台词 + 选项 + 路由），
   160 个接近 1.6MB，都是静态数组，启动 memset 一次，不影响运行。
   改这个宏会改变 read_bits[] 的字节数 → 存档格式变 → SAVE_VERSION 必须同步 +1。 */
#define MAX_NODES   160
#define MAX_LINES    16
#define MAX_TEXT    512
#define MAX_CHOICES   4
#define MAX_ID       32
#define MAX_LOG     128
#define MAX_ROUTES   72             /* hub 一个节点就挂了 45 条自动路由（每个事件一条），
                                       原来 40 条刚好卡死，加事件直接报错。留一倍余量 */

/* 事件编号占的格子数：id = 字母下标*16 + 数字，字母 A-Z、数字 1-15。
   seen[] 的长度、event_id_of() 的上界、存档布局都引用这一个宏。 */
#define EVENT_SLOTS  416

/* 一行台词。speaker 为空表示旁白。 */
typedef struct {
    char speaker[40];
    char text[MAX_TEXT];
} Line;

/* 一个分支选项。target 以 '@' 开头表示进入 AI 支线，
   格式 "@角色id:返回节点id"。

   剧本里一行最多六列，**前四列对所有分支同义**：
     -> 选项文字|目标|玩家这句话|好感度增减|本分支场景|出现条件
   第 5 列的场景只对 AI 分支有意义（普通分支存了也不读）；
   第 6 列是显示条件：
     aff>=46   好感度到 46 才出现
     aff>70    …
     aff<=20   …
     aff==0    …
   不满足条件时这个选项直接不显示。 */
typedef struct {
    char text[80];
    char target[MAX_ID];
    char prompt[160];    /* 第三列：玩家这句话，进提示词与「已知信息」 */
    char scene[160];     /* 第五列：场景覆盖，只有 AI 分支会读 */
    int  affinity;       /* 第四列：好感度增减，缺省 1（保持旧的 +1 行为） */
    int  cond_min;       /* 第六列解析出的下限，默认 0 */
    int  cond_max;       /* 第六列解析出的上限，默认 100 */
    int  cond_events;    /* 第六列的 ev>=N：已触发过的事件数下限，默认 0。

                            hub 里「学期最后一天」这种收尾出口必须靠它挡住：
                            否则玩家一进第二章就能点它直接跳结局，几十个
                            事件一个都不用演。 */
} Choice;

/* 自动路由：节点开头写 `=> aff>=46 目标节点`，进节点时按好感度自动跳。
   多条路由从上往下取第一条命中的，所以高门槛的写在前面。 */
typedef struct {
    int  cond_min;
    int  cond_max;
    int  meter;          /* 用哪条槽判定：METER_AFF（好感）或 METER_BOND（距离） */
    char target[MAX_ID];
} Route;

/* 剧本节点 = 若干连续台词 + 节点末的分支选项。 */
typedef struct {
    char   id[MAX_ID];
    Line   lines[MAX_LINES];
    int    line_count;
    Choice choices[MAX_CHOICES];
    int    choice_count;
    Route  routes[MAX_ROUTES];
    int    route_count;
    char   bg[32];          /* 本节点的背景图名，空 = 沿用上一张 */
    char   spr[48];         /* 本节点的立绘，形如 "shenzhiji:soft"，空 = 沿用 */
    char   loc[64];         /* 本节点的地点，空 = 沿用 */
    char   event[256];      /* 本节点触发的事件，写进提示词的“最近事件” */
    char   offline[600];    /* 本节点自己的离线台词（没配 key 时用），空 = 用角色卡的 */
    int    once_id;         /* @once A2 解析出的编号：这个事件只触发一次 */
    char   toast[64];       /* 进这个节点时右上角冒一句提示，空 = 不冒 */
    int    meter;           /* @meter bond：选项的数值加进 bond（距离/心结），不加好感 */
} Node;

/* 选项数值加到哪一条槽：默认好感；H 段用 bond 记录“距离”，免得 +2/+3 全部落在
   封顶的好感上、变成纯装饰。 */
#define METER_AFF   0
#define METER_BOND  1

/* ============ 界面状态 ============ */

typedef enum {
    STATE_TITLE = 0,
    STATE_GAME,
    STATE_SAVE,
    STATE_LOAD,
    STATE_LOG,
    STATE_CONFIG,
    STATE_INPUT          /* 自由输入：玩家自己打一句话丢给 AI */
} GameState;

/* ============ 对话历史 ============ */

typedef struct {
    Line entries[MAX_LOG];
    int  count;
} LogBook;

/* ============ 存档 ============ */
/* 用 magic + version 做兼容性判断，字段逐个写入，
   不直接 fwrite 整个结构体，避免编译器对齐差异。 */

#define SAVE_MAGIC    0x47414C31u   /* "GAL1" */
#define SAVE_VERSION  8u            /* v3 背景名，v4 事件位图，v5 96 字节，v6 416 字节，
                                       v7 加 bond，v8 节点上限 96→160（read_bits 变长） */
#define SAVE_SLOTS    3

typedef struct {
    int       exists;
    long long timestamp;      /* Unix 秒 */
    int       node_index;
    int       line_index;
    int       affinity;
    char      preview[96];    /* 存档时的台词，用于槽位列表展示 */
} SaveSlotInfo;

/* ============ 运行时 ============ */

typedef struct {
    /* 静态剧本 */
    Node nodes[MAX_NODES];
    int  node_count;

    /* 可存档状态 */
    int  node_index;
    int  line_index;
    int  affinity;
    int  bond;                   /* H 段“距离/心结”：0-100，独立于好感 */
    int  read_bits[MAX_NODES];   /* 每节点一个 bitmask，标记该行是否读过 */
    char bg[32];                 /* 当前背景图名，入档，读档后画面一致 */
    char spr[48];                /* 当前立绘，形如 "shenzhiji:soft" 或 "off" */
    char location[64];           /* 当前地点，喂给提示词 */
    char last_event[256];        /* 最近发生的事件，喂给提示词 */
    unsigned char seen[EVENT_SLOTS];  /* 已触发过的事件标记，下标 = 事件编号 */
    char event_history[1024];    /* 已经发生过的事件清单，进提示词，让她“记得” */
    unsigned int enter_seq;      /* 每次进节点 +1，用来判断“又进了一次同一个节点” */

    /* 瞬时状态，不入档 */
    int  waiting_choice;
    int  finished;
    int  state;                  /* GameState */

    LogBook log;

    /* “已知信息”：玩家选过的话 + 她回过的句子，滚动保留最近若干条，
       每次请求 AI 支线时整段塞进提示词。不入档。 */
    char ai_memory[2048];

    /* AI 支线临时节点：生成结果放这里播放，播完跳 ai_return_index */
    Node ai_node;
    int  ai_active;
    int  ai_return_index;
} Game;

/* 剧本 */
int  event_id_of(const char *s);   /* "A2" -> 12，用于 @once */

/* 剧本解析的单行入口：只做词法和装配，不碰文件、不改全局状态。
   这是唯一一处「文本怎么写 → 结构体怎么填」的实现，校验器和测试都走它。 */
typedef enum {
    SL_SKIP = 0,   /* 空行 / 注释 */
    SL_NODE,       /* :: 节点id */
    SL_CHOICE,     /* -> 选项 */
    SL_ROUTE,      /* => 自动路由 */
    SL_META,       /* @bg / @char / @loc / @event / @offline / @once / @toast */
    SL_LINE,       /* 台词或旁白 */
    SL_ERROR       /* 语法错误，原因写进 err */
} ScriptLineKind;

ScriptLineKind script_parse_line(const char *line, Node *n, char *err, int err_size);

int  script_load(Game *g, const char *path);
int  script_error_count(void);     /* 上次 script_load 里报的行级错误数 */
int  script_find(const Game *g, const char *id);
void game_enter_node(Game *g, const char *id);
void game_enter_index(Game *g, int index);

/* 只把节点状态铺开，不跑自动路由。读档用：存档里记的已经是路由之后的节点，
   再跑一次会被当前好感度带到别处去。 */
void game_restore_index(Game *g, int index);
int  game_advance(Game *g);
const Node *game_current_node(const Game *g);
const Line *game_current_line(const Game *g);
void game_restart(Game *g);

/* AI 支线选项："@角色id:返回节点id" */
int  choice_is_ai(const Choice *c);
void choice_split_ai(const Choice *c, char *char_id, int char_size,
                     char *ret_id, int ret_size);

/* 选项在当前好感度下是否显示（第六列条件）。
   seen_events 是已经触发过的事件数，对应条件里的 ev>=N。 */
int  choice_visible(const Choice *c, int affinity, int seen_events);

/* 已经触发过的事件总数（数 Game.seen 里的位）。给 ev>=N 条件用，
   也用来判断“这一局到底演了多少内容”。 */
int  game_seen_count(const Game *g);

/* 模型回复的解析：把返回的文本拆成若干行台词，顺便取状态栏里的判定值。
   和剧本一样是「文本 → 台词行」，所以放在 script.c 里，共用同一套测试。
   返回值 1 表示找到了状态栏并取到了判定，*judgement 才是有效值；
   返回 0 时 *judgement 为 0（离线兜底台词就没有状态栏）。 */
int parse_reply(const char *reply, Node *out, int *judgement);

/* save.c */
int save_write(const Game *g, int slot);
int save_read(Game *g, int slot);
int save_exists(int slot);
int save_slot_info(int slot, SaveSlotInfo *info);

/* util.c */
int  utf8_char_count(const char *s);
int  utf8_truncate(char *dst, int dst_size, const char *src, int max_chars);
void log_push(Game *g, const char *speaker, const char *text);

#endif
