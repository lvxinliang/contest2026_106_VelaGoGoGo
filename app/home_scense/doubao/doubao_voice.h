/****************************************************************************
 * app/home_scense/doubao/doubao_voice.h
 * Public session API for the Doubao full-duplex continuous voice chat.
 ****************************************************************************/

#ifndef HOME_SCENSE_DOUBAO_VOICE_H
#define HOME_SCENSE_DOUBAO_VOICE_H

#include "doubao_config.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct doubao_history_turn_s
{
  char user_text[DOUBAO_TEXT_MAX];
  char assistant_text[DOUBAO_REPLY_MAX];
} doubao_history_turn_t;

typedef enum doubao_voice_state_e
{
  DOUBAO_VOICE_UNCONFIGURED,
  DOUBAO_VOICE_IDLE,             /* 会话关闭/待命,等待"开始会话" */
  DOUBAO_VOICE_CONNECTING,       /* 建连中 */
  DOUBAO_VOICE_LISTENING,        /* 全双工会话活跃、持续上传、等待用户说话 */
  DOUBAO_VOICE_RECORDING,        /* 服务端 VAD 判定用户正在说话 */
  DOUBAO_VOICE_WAITING_RESPONSE, /* 等待 LLM/TTS 回复 */
  DOUBAO_VOICE_PLAYING,          /* TTS 播放中(此间门控麦克风上传) */
  DOUBAO_VOICE_ERROR,
} doubao_voice_state_t;

typedef struct doubao_voice_snapshot_s
{
  doubao_voice_state_t state;
  char user_text[DOUBAO_TEXT_MAX];
  char assistant_text[DOUBAO_REPLY_MAX];
  char error_text[DOUBAO_ERROR_MAX];
  unsigned turn_seq;          /* 每轮对话开始 +1,供 UI 沉淀历史气泡 */
  unsigned history_revision;  /* 服务端历史加载完成后 +1,供 UI 重画气泡 */
} doubao_voice_snapshot_t;

int doubao_voice_init(void);
void doubao_voice_deinit(void);

/* 全双工连续会话:开始/停止。start 置 talking=true 进入连续对话,stop 置
 * talking=false 恢复待命(连接常驻,零等待重启)。 */
int doubao_voice_start(void);
int doubao_voice_stop(void);

/* 开启全双工连续会话,并在会话建立后自动发送一条 greeting 提示词让豆包
 * 主动打招呼(如正脸唤醒:"有人正在看你,主动跟他打个招呼")。之后照常
 * 全双工聆听用户。greeting 为空则等同 doubao_voice_start()。 */
int doubao_voice_start_greeting(const char *greeting);

/* 立即中止任何在播 TTS(全双工/文字问答)并回到待命,尽快释放播放设备。
 * 供音乐播放等抢占使用。 */
int doubao_voice_abort_playback(void);

/* 提交一句文字 query,豆包回文字气泡 + TTS 语音(复用常驻连接)。
 * 0 受理;-EBUSY 全双工中或上条未完;-EAGAIN 连接未就绪;-ENOKEY 未配置;
 * -EINVAL 空文本。 */
int doubao_voice_ask_text(const char *text);

/* 唤醒相关组合原语——当前版本不接 KWS,统一 stub 返回 -ENOSYS。接口保留以便
 * 后续接入外部唤醒进程与 face_tracker。 */
int doubao_voice_start_wake_turn(void);
int doubao_voice_greet_then_wake(const char *text);
int doubao_voice_greet_then_talk(const char *text);

int doubao_voice_reset(void);
bool doubao_voice_is_configured(void);
/* 当前是否处于全双工语音对话中(talking)。供轮播页门控 set_emoji/ask_text。 */
bool doubao_voice_is_talking(void);
void doubao_voice_get_snapshot(doubao_voice_snapshot_t *snapshot);
size_t doubao_voice_get_history(doubao_history_turn_t *turns, size_t capacity,
                                unsigned *revision);

#endif /* HOME_SCENSE_DOUBAO_VOICE_H */
