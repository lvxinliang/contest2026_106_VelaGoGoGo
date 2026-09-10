/****************************************************************************
 * app/home_scense/doubao/voice_player.h
 * Streaming PCM playback interface for TTS data.
 ****************************************************************************/

#ifndef HOME_SCENSE_VOICE_PLAYER_H
#define HOME_SCENSE_VOICE_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct voice_player_s voice_player_t;

/* 播放中止查询回调:vp_play 每轮轮询,返回 true 则立即停播收尾。用于让
 * "停止对话/人脸离开/音乐抢占"在同步播放期间也能及时打断,避免长回复
 * 播放把会话线程冻结数秒(期间状态切换丢失 → 采集不重开、状态卡死)。 */
typedef bool (*voice_player_abort_cb_t)(void *arg);

int voice_player_open(voice_player_t **player, const char *device,
                      uint32_t sample_rate, uint8_t channels,
                      uint8_t bits_per_sample);
int voice_player_write(voice_player_t *player, const uint8_t *data,
                       size_t size);

/* 设置播放中止查询回调(在 open 之后、close 之前调用)。 */
void voice_player_set_abort_cb(voice_player_t *player,
                               voice_player_abort_cb_t cb, void *arg);

void voice_player_close(voice_player_t *player);

/* 立即中止播放并释放:丢弃尚未播出的缓冲(不 drain 尾音),用于 barge-in /
 * 音乐抢占——须立刻静音,不能像 close 那样把剩余 TTS 尾音放完(否则尾音
 * 继续外放又被麦克风采回)。 */
void voice_player_abort(voice_player_t *player);

#endif /* HOME_SCENSE_VOICE_PLAYER_H */
