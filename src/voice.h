#ifndef VOICE_H
#define VOICE_H

/* 台词配音（离线固定剧本的那一部分）。
 *
 * 素材在 data/voice/<节点>_<行号>.ogg，由 tools/gen_voice.py 预生成：
 * 行号就是引擎里 line_index，旁白也占位，所以文件名能一一对上。
 *
 * 播放策略：
 *   * start_line() 时调 voice_play_line()，一行一句，切行即停上一句
 *   * 推进 / 切节点 / 读档 / 回标题 / 开菜单 / 快进 都只调 voice_stop()
 *   * 文件缺失 = 这一句没配音，静默跳过（和背景图、音效一个策略）
 *   * 谁念哪句：只有剧本里写了说话人的行才配，旁白（speaker 为空）不配
 *
 * 开关：V 键在游戏里切换；启动前设 GAL_VOICE=0 可以全局关掉（录屏/调试用）。
 */

void voice_init(void);
void voice_shutdown(void);
void voice_update(void);         /* 每帧：回收播完的槽位 */

/* 播这一句。node_id 是剧本节点 id，line_index 是节点内行号（含旁白的占位）。 */
void voice_play_line(const char *node_id, int line_index, const char *speaker);
void voice_stop(void);

void voice_set_enabled(int on);
int  voice_enabled(void);

void voice_set_volume(float v);  /* 0.0 - 1.0，默认 0.9 */
float voice_volume(void);

int  voice_is_playing(void);
int  voice_loaded_count(void);   /* 已缓存的句数，调试/自检用 */

#endif
