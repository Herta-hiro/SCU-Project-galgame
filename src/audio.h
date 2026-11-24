#ifndef AUDIO_H
#define AUDIO_H

/* 背景音与界面音效。
 *
 * 素材在 data/audio/，由 tools/gen_audio.py 合成（雨声是滤波噪声，不是采样），
 * 换文件或改参数重跑都行，不用动代码。
 *
 * 和背景图 / 立绘一个策略：文件缺失就静默禁用，不报错、不卡流程。
 * 没有声卡（某些远程桌面、CI）时也只打印一行提示。
 */

void audio_init(void);
void audio_update(void);        /* 每帧调用，给音乐流喂数据 */
void audio_shutdown(void);

void audio_se_click(void);      /* 推进对话、点选项 */
void audio_se_page(void);       /* 翻记录、开存读档 */

/* 环境音（雨声）开关。默认开，但音量压得很低。
 * 玩家可能只想听台词，按 M 就能把雨声关掉，界面音效和配音不受影响。 */
void audio_set_bgm_enabled(int on);
int  audio_bgm_enabled(void);

#endif
