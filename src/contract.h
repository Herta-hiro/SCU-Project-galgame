#ifndef CONTRACT_H
#define CONTRACT_H

/* 编译期契约。
 *
 * 同一组字节上限在 character.h（角色卡能存多少）和 ai.h（提示词能装多少）
 * 各写了一遍，两边必须相等，否则超长人设会在拼提示词时被静默截断。
 * 以前靠人眼对齐，这里改成让编译器管：改一处漏一处，编译就失败。
 *
 * 这个头文件由 ai.c 和 character.c 各 include 一次（都有编译，多余无害）。
 */

#include "game.h"
#include "character.h"
#include "ai.h"

_Static_assert(CARD_PERSONA_MAX == AI_PERSONA_MAX, "persona 上限两处不一致");
_Static_assert(CARD_RULES_MAX   == AI_RULES_MAX,   "rules 上限两处不一致");
_Static_assert(CARD_SCENE_MAX   == AI_SCENE_MAX,   "scene 上限两处不一致");
_Static_assert(CARD_LINE_MAX    == AI_INPUT_MAX,   "line / user_input 上限两处不一致");
_Static_assert(CARD_TONE_MAX    == AI_TONE_MAX,    "stage_tone 上限两处不一致");
_Static_assert(CARD_OFFLINE_MAX == AI_OFFLINE_MAX, "offline 上限两处不一致");
_Static_assert(CARD_CALL_MAX    == AI_CALL_MAX,    "称呼上限两处不一致");
_Static_assert(CARD_EVENTS_MAX  == AI_EVENTS_MAX,  "事件库上限两处不一致");

_Static_assert(sizeof(((Node *)0)->loc)   == AI_LOC_MAX,        "地点上限两处不一致");
_Static_assert(sizeof(((Node *)0)->event) == AI_LAST_EVENT_MAX, "最近事件上限两处不一致");
_Static_assert(sizeof(((Game *)0)->event_history) == AI_HISTORY_MAX, "事件清单上限两处不一致");
_Static_assert(sizeof(((Game *)0)->ai_memory)     == AI_MEMORY_MAX,  "已知信息上限两处不一致");

/* 已读位图是每个节点一个 32 位 int，行号不能超出去 */
_Static_assert(MAX_LINES <= 32, "read_bits 每节点只有 32 位");

/* 事件编号 = 字母 A-Z × 数字 1-15，按 16 进制分段，最大值必须落在 seen[] 里 */
_Static_assert(EVENT_SLOTS == 26 * 16, "EVENT_SLOTS 必须等于 event_id_of 的上界");

#endif
