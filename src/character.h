#ifndef CHARACTER_H
#define CHARACTER_H

/* 角色卡（人设）加载层。
 *
 * 人设不再写死在 main.c 里，而是从 data/characters.gal 读：
 * 补人设、改语气、换场景、写各阶段台词都只动数据文件。
 *
 * 角色卡格式（UTF-8）：
 *
 *   :: shenzhiji                 角色 id，剧本里用 @shenzhiji:返回节点 引用
 *   name 沈知霁                   显示名，也会写进给模型的提示词
 *   persona 二十一岁，成都……       人设：外貌、性格、说话方式、讨厌什么
 *   persona 说话短句，少语气词。     同一个键写多行 = 自动换行拼接
 *   rules 严格按当前阶段说话。       行为规则，多行拼接
 *   scene 成都秋雨天，省图三楼。     当前场景，剧本第四列可覆盖
 *   line 玩家坐到对面，把伞收好。     默认“玩家这句话”，剧本第三列可覆盖
 *   player_call 你                 女方怎么称呼玩家
 *   her_call 你                    玩家怎么称呼女方
 *   stage0 公事公办，礼貌疏离。      各阶段语气，0-4 共五档
 *   offline0 沈知霁：椅子没人。      没配 API key 时的离线兜底台词，按阶段取
 *   # 注释
 *
 * 没有 characters.gal 时退回内置默认卡，行为与旧版一致。
 */

#include "game.h"

#define MAX_CARDS          16
#define CARD_NAME_MAX      40
#define CARD_PERSONA_MAX 1600
#define CARD_RULES_MAX   1600
#define CARD_SCENE_MAX    600
#define CARD_LINE_MAX     600
#define CARD_TONE_MAX     300
#define CARD_OFFLINE_MAX  600
#define CARD_CALL_MAX      40
#define CARD_EVENTS_MAX 14000

typedef struct {
    char id[MAX_ID];
    char name[CARD_NAME_MAX];
    char persona[CARD_PERSONA_MAX];
    char rules[CARD_RULES_MAX];
    char scene[CARD_SCENE_MAX];
    char line[CARD_LINE_MAX];
    char player_call[CARD_CALL_MAX];
    char her_call[CARD_CALL_MAX];
    char stage_tone[5][CARD_TONE_MAX];   /* stage0..stage4 */
    char offline[5][CARD_OFFLINE_MAX];   /* offline0..offline4 */
    char events[CARD_EVENTS_MAX];        /* 事件库：触发条件 + 行为脚本 + 好感结算 */
} CharacterCard;

/* 读角色卡，返回成功加载的卡数；0 = 文件缺失或没有有效卡。 */
int characters_load(const char *path);

int  characters_count(void);
const CharacterCard *character_find(const char *id);

/* 好感度 → 阶段序号 0..4（0-20 / 21-45 / 46-70 / 71-85 / 86-100）。 */
int  character_stage_of(int affection);

/* 阶段序号 → 固定标签，写进提示词的“当前阶段”。 */
const char *character_stage_name(int stage);

/* 找不到角色时的兜底卡，永远非空。 */
const CharacterCard *character_default(void);

#endif