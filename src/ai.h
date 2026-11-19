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
#define AI_EVENTS_MAX  7000
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
} AiPrompt;

void ai_init(void);
void ai_shutdown(void);

int  ai_is_configured(void);
void ai_set_config(const char *api_key, const char *model);

/* 发起一次生成。返回 1 表示已开始（或命中缓存/离线兜底），0 表示无法发起。 */
int  ai_request(const AiPrompt *p);

/* 每帧调用，推进状态机 */
void ai_poll(void);

int         ai_state(void);
int         ai_from_cache(void);
const char *ai_reply(void);
const char *ai_error(void);

/* 纯函数，可离线单测 */
int ai_build_request_json(const AiPrompt *p, char *out, int out_size);
int ai_parse_sse_line(const char *line, char *delta, int delta_size);

#endif