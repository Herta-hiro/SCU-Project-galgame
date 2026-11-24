#ifndef AI_H
#define AI_H

/* AI 支线接口层。
 *
 * 设计上有三道保险，都是为了让课堂演示不会死在网络上：
 *   1. 同一份 prompt 生成过一次就写缓存，第二次直接读缓存，秒出且可复现
 *   2. 没配置 API key 时返回角色卡里的离线兜底台词（按好感阶段取），流程照常走通
 *   3. 请求跑在独立线程里，主循环只 poll 状态，界面不会卡死
 */

#define AI_REPLY_MAX   4096
#define AI_DELTA_MAX    512

#define AI_PERSONA_MAX 1600
#define AI_RULES_MAX   1600
#define AI_SCENE_MAX    600
#define AI_INPUT_MAX    600
#define AI_MEMORY_MAX  2048
#define AI_OFFLINE_MAX  600
#define AI_TONE_MAX     300
#define AI_STAGE_MAX     32
#define AI_CALL_MAX      40
#define AI_EVENTS_MAX 14000
#define AI_LOC_MAX       64
#define AI_LAST_EVENT_MAX 256
#define AI_HISTORY_MAX  1024

typedef enum {
    AI_IDLE = 0,
    AI_BUSY,
    AI_DONE,
    AI_FAILED
} AiState;

/* 一次生成请求的全部上下文。字段和提示词模板一一对应：
 * 角色卡负责 persona / rules / stage_tone / 称呼 / 离线台词，
 * 剧本负责 user_input / scene，运行时补 affection / stage / memory。 */
typedef struct {
    char character_id[32];
    char character_name[40];
    char persona[AI_PERSONA_MAX];   /* 角色设定：外貌、性格、说话方式 */
    char rules[AI_RULES_MAX];       /* 行为规则与禁忌 */
    char scene[AI_SCENE_MAX];       /* 当前场景 */
    char user_input[AI_INPUT_MAX];  /* 玩家这句话 */
    char player_call[AI_CALL_MAX];  /* 她怎么称呼玩家 */
    char her_call[AI_CALL_MAX];     /* 玩家怎么称呼她 */
    char stage[AI_STAGE_MAX];       /* 当前阶段标签 */
    char stage_tone[AI_TONE_MAX];   /* 当前阶段语气 */
    char memory[AI_MEMORY_MAX];     /* 已知信息：最近发生过什么 */
    char events[AI_EVENTS_MAX];     /* 事件库：A1~E2，含触发条件与结算 */
    char location[AI_LOC_MAX];      /* 当前地点 */
    char last_event[AI_LAST_EVENT_MAX]; /* 最近发生的事件 */
    char history[AI_HISTORY_MAX];   /* 已经发生过的事件清单（她应该记得） */
    char offline[AI_OFFLINE_MAX];   /* 没配 key 时的兜底台词 */
    int  affection;                 /* 好感度 0..100 */
    int  stage_index;               /* 阶段编号 0..4，裁事件库用（stage 是给人看的标签） */

    /* 自由输入专用：绕过缓存。
     *
     * 剧本里的 AI 分支是固定台词，缓存起来秒出且可复现，很合适。
     * 但玩家随口说的话一旦命中缓存，同样的输入就永远得到同样的回答——
     * 说了一句不合适的话，再想补救也是原样重复，看起来就像「卡在一句话上」。
     * 所以自由输入每次现场生成，不进缓存也不读缓存。 */
    int  no_cache;
} AiPrompt;

void ai_init(void);
void ai_shutdown(void);

int  ai_is_configured(void);
void ai_set_config(const char *api_key, const char *model);

/* 读取当前内存里的 key（未配置返回空串）；把 key/model 写回 data/ai.conf 并同步内存。
   model 传 NULL 时保留现有模型。 */
const char *ai_get_key(void);
void ai_save_config(const char *api_key, const char *model);

/* 发起一次生成。返回 1 表示已开始（或命中缓存/离线兜底），0 表示无法发起
 * （上一次请求还在跑）。返回 0 时调用方必须自己恢复界面，否则会一直等。 */
int  ai_request(const AiPrompt *p);

/* 放弃当前这次等待：主循环立即恢复界面，后台线程的结果作废。
 * 用于「等太久玩家按 Esc」和客户端超时。 */
void ai_abandon(void);

/* 每帧调用，推进状态机 */
void ai_poll(void);

int         ai_state(void);
int         ai_from_cache(void);
const char *ai_reply(void);
const char *ai_error(void);

/* 流式输出：请求进行中时，把已经收到的部分文本拷出来。
 *
 * 以前是「等整段生成完 → 一次性显示」，首字延迟三到八秒，期间界面只有一个
 * 「正在生成……」的遮罩，体感非常慢。现在边收边显示，第一句话几乎立刻出来，
 * 玩家能眼看着字一个个长出来。
 *
 * 返回拷贝到的字节数（0 表示还没有内容）。调用方每帧拷一次即可。 */
int  ai_partial_copy(char *out, int size);

/* 纯函数，可离线单测 */
int ai_build_request_json(const AiPrompt *p, char *out, int out_size);
int ai_parse_sse_line(const char *line, char *delta, int delta_size);

/* 从事件库里挑出**当前阶段用得上**的条目。
 *
 * 事件库有 46 条、11KB 左右，全塞进 system prompt 每次都要多烧几千 token，
 * 首字延迟和费用都翻倍，而其中九成条目在这个好感档位根本不会触发。
 * 每条事件的格式是「events A1 名称｜好感5-15：……」，所以按好感区间筛：
 *
 *   保留条件 = 区间与 [aff - 8, aff + 20] 有交集
 *
 * 往前留 8 点是因为刚演过的事件模型可能还要引用；往后留 20 点是让它知道
 * “再往前一点会遇到什么”，写反应时有个方向。
 * 没有好感标记的（H1–H9 写的是「第N天」）只在 stage>=4 时保留。
 *
 * 返回写入 out 的字节数。aff/stage 之外的条目会被整行丢掉。
 */
int ai_trim_events(const char *events, int aff, int stage, char *out, int out_size);

#endif