/****************************************************************************
 * app/home_scense/doubao/voice_player.c
 * PCM playback for Doubao TTS.
 *
 * 关键设计(避免欠载/截断/驱动崩溃):
 *  流式边收边播在本板 sunxi 驱动上不可行——驱动硬件 PCM 缓冲极小(≈42ms)、
 *  仅 2 个 app buffer,网络供数稍有间隙即欠载(snd_vela_pcm_writei -EPIPE /
 *  Xrun),且欠载→STOP 时驱动会 Data abort。
 *
 *  故改为:voice_player_write 先把整轮 TTS PCM 累积到内存;voice_player_close
 *  时数据已收齐(TTS_ENDED 在所有音频帧之后到达),再用与 wakeup/wav_player.c
 *  相同的、本板验证过的 AUDIOIOC 播放时序一次性播完——refill 源在内存,
 *  瞬时 memcpy 永不欠载;数据全部送入声卡 + 尾余量后才 STOP 一次,不截断;
 *  等驱动回 COMPLETE 再清理。
 *
 *  abort(打断/音乐抢占/挂断)直接丢弃累积数据,不播放。
 ****************************************************************************/

#include "voice_player.h"

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define VP_MAX_BUFFERS      16
#define VP_POLL_MS          200    /* mq_timedreceive 单次等待上限 */
#define VP_TAIL_MARGIN_MS   150    /* 缓冲全取走后声卡环形残余余量 */
#define VP_STOP_GRACE_MS    500    /* STOP 后等 COMPLETE 宽限 */
#define VP_EXTRA_TIMEOUT_MS 3000   /* 兜底超时(音频时长之外再加) */
#define VP_INIT_CAP         (64 * 1024)
#define VP_MAX_CAP          (6 * 1024 * 1024)  /* 累积上限 ≈128s,防失控 */

struct voice_player_s
{
  char      device[48];
  uint32_t  rate;
  uint8_t   channels;
  uint8_t   bits;
  uint8_t  *data;      /* 累积的 PCM */
  size_t    size;
  size_t    cap;
  volatile int aborted;
};

static uint64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* 播放累积的 PCM。时序完全对齐 wakeup/wav_player.c 的 wav_play_mem:
 * RESERVE→CONFIGURE→REGISTERMQ→ALLOCBUFFER×N→预填 ENQUEUE→START→
 * mq 循环 DEQUEUE 补数据,数据耗尽且缓冲全取走后睡尾余量→STOP 一次→
 * 等 COMPLETE。返回 0 表示整段播完。 */
static int vp_play(voice_player_t *player)
{
  struct ap_buffer_s *bufs[VP_MAX_BUFFERS];
  struct audio_msg_s msg;
  struct audio_buf_desc_s buf_desc;
  struct audio_caps_desc_s cap_desc;
  struct ap_buffer_info_s buf_info;
  struct mq_attr attr;
  struct timespec ts;
  char mqname[24];
  mqd_t mq = (mqd_t)-1;
  int fd = -1;
  int allocated = 0;
  int nqueued = 0;
  int outstanding = 0;
  size_t pos = 0;
  size_t remaining = player->size;
  bool streaming;
  bool stopping = false;
  bool running = true;
  bool completed = false;
  bool clean_stop = false;
  uint64_t deadline_ms;
  int ret;
  int x;

  fd = open(player->device, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      syslog(LOG_ERR, "[voice] open %s failed: %d\n", player->device, -errno);
      return -errno;
    }
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len          = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type         = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_subtype      = AUDIO_FMT_PCM;
  cap_desc.caps.ac_channels     = player->channels;
  cap_desc.caps.ac_chmap        = 0;
  cap_desc.caps.ac_controls.hw[0] = player->rate;
  cap_desc.caps.ac_controls.b[2]  = player->bits;
  cap_desc.caps.ac_controls.b[3]  = 0;
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&cap_desc) < 0)
    {
      syslog(LOG_ERR, "[voice] CONFIGURE %luHz/%uch/%ubit failed: %d\n",
             (unsigned long)player->rate, player->channels, player->bits,
             -errno);
      ret = -errno;
      goto out;
    }

  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&buf_info) < 0)
    {
      buf_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;
      buf_info.nbuffers    = CONFIG_AUDIO_NUM_BUFFERS;
    }

  snprintf(mqname, sizeof(mqname), "/tmp/vp%lx",
           (unsigned long)(uintptr_t)&bufs);
  attr.mq_maxmsg  = buf_info.nbuffers + 8;
  attr.mq_msgsize = sizeof(msg);
  attr.mq_curmsgs = 0;
  attr.mq_flags   = 0;
  mq = mq_open(mqname, O_RDWR | O_CREAT, 0644, &attr);
  if (mq == (mqd_t)-1)
    {
      ret = -errno;
      goto out;
    }
  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (uintptr_t)mq) < 0)
    {
      ret = -errno;
      goto out;
    }

  for (x = 0; x < buf_info.nbuffers && x < VP_MAX_BUFFERS; ++x)
    {
      memset(&buf_desc, 0, sizeof(buf_desc));
      buf_desc.numbytes  = buf_info.buffer_size;
      buf_desc.u.pbuffer = &bufs[x];
      if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&buf_desc) !=
          (int)sizeof(buf_desc))
        {
          ret = -EIO;
          goto out;
        }
      allocated++;
    }
  if (allocated == 0)
    {
      ret = -EIO;
      goto out;
    }

  /* 预填:尽量灌满所有缓冲再 START */
  for (x = 0; x < allocated && remaining > 0; ++x)
    {
      size_t n = bufs[x]->nmaxbytes < remaining ? bufs[x]->nmaxbytes
                                                : remaining;
      memcpy(bufs[x]->samp, player->data + pos, n);
      pos += n;
      remaining -= n;
      bufs[x]->nbytes  = n;
      bufs[x]->curbyte = 0;
      bufs[x]->flags   = 0;

      memset(&buf_desc, 0, sizeof(buf_desc));
      buf_desc.numbytes = n;
      buf_desc.u.buffer = bufs[x];
      if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&buf_desc) < 0)
        {
          ret = -errno;
          goto out;
        }
      nqueued++;
      outstanding++;
    }
  if (nqueued == 0)
    {
      ret = 0;
      goto out;
    }
  streaming = remaining > 0;

  if (ioctl(fd, AUDIOIOC_START, 0) < 0)
    {
      ret = -errno;
      goto out;
    }

  deadline_ms = now_ms() +
      (uint64_t)player->size * 1000 /
          ((uint64_t)player->rate * player->channels * (player->bits / 8)) +
      VP_EXTRA_TIMEOUT_MS;

  while (running)
    {
      unsigned prio;

      /* abort:立即 STOP 收尾,丢弃剩余 */
      if (player->aborted && !stopping)
        {
          ioctl(fd, AUDIOIOC_STOP, 0);
          stopping = true;
          streaming = false;
          deadline_ms = now_ms() + VP_STOP_GRACE_MS;
        }

      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += (long)VP_POLL_MS * 1000000L;
      if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

      if (mq_timedreceive(mq, (char *)&msg, sizeof(msg), &prio, &ts) !=
          (ssize_t)sizeof(msg))
        {
          if (now_ms() < deadline_ms)
            {
              continue;
            }
          if (!stopping)
            {
              ioctl(fd, AUDIOIOC_STOP, 0);
              stopping = true;
              deadline_ms = now_ms() + VP_STOP_GRACE_MS;
            }
          else
            {
              break;
            }
          continue;
        }

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DEQUEUE:
            {
              struct ap_buffer_s *apb = (struct ap_buffer_s *)msg.u.ptr;

              outstanding--;
              if (!stopping && streaming && apb != NULL)
                {
                  size_t n = apb->nmaxbytes < remaining ? apb->nmaxbytes
                                                        : remaining;
                  memcpy(apb->samp, player->data + pos, n);
                  pos += n;
                  remaining -= n;
                  apb->nbytes  = n;
                  apb->curbyte = 0;
                  apb->flags   = 0;

                  memset(&buf_desc, 0, sizeof(buf_desc));
                  buf_desc.numbytes = n;
                  buf_desc.u.buffer = apb;
                  if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER,
                            (uintptr_t)&buf_desc) == 0)
                    {
                      outstanding++;
                      if (remaining == 0)
                        {
                          streaming = false;
                        }
                    }
                  else
                    {
                      streaming = false;
                    }
                }
              else if (!stopping && !streaming && outstanding == 0)
                {
                  /* 所有数据已入声卡:睡掉环形残余再 STOP 一次 */
                  usleep(VP_TAIL_MARGIN_MS * 1000);
                  ioctl(fd, AUDIOIOC_STOP, 0);
                  stopping = true;
                  clean_stop = true;
                }
            }
            break;

          case AUDIO_MSG_STOP:
            if (!stopping)
              {
                stopping = true;
                ioctl(fd, AUDIOIOC_STOP, 0);
              }
            break;

          case AUDIO_MSG_COMPLETE:
            completed = true;
            running = false;
            break;

          default:
            break;
        }
    }

  ret = (completed && clean_stop) ? 0 : -ETIMEDOUT;

out:
  for (x = 0; x < allocated; ++x)
    {
      memset(&buf_desc, 0, sizeof(buf_desc));
      buf_desc.u.buffer = bufs[x];
      ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&buf_desc);
    }
  if (mq != (mqd_t)-1)
    {
      ioctl(fd, AUDIOIOC_UNREGISTERMQ, (uintptr_t)mq);
    }
  ioctl(fd, AUDIOIOC_RELEASE, 0);
  close(fd);
  if (mq != (mqd_t)-1)
    {
      mq_close(mq);
      mq_unlink(mqname);
    }
  return ret;
}

static void vp_free(voice_player_t *player)
{
  if (player)
    {
      free(player->data);
      free(player);
    }
}

int voice_player_open(voice_player_t **out, const char *device,
                      uint32_t sample_rate, uint8_t channels,
                      uint8_t bits_per_sample)
{
  voice_player_t *player;

  if (!out || !device)
    {
      return -EINVAL;
    }

  player = calloc(1, sizeof(*player));
  if (!player)
    {
      return -ENOMEM;
    }
  player->data = malloc(VP_INIT_CAP);
  if (!player->data)
    {
      free(player);
      return -ENOMEM;
    }
  player->cap      = VP_INIT_CAP;
  player->size     = 0;
  player->rate     = sample_rate;
  player->channels = channels;
  player->bits     = bits_per_sample;
  strncpy(player->device, device, sizeof(player->device) - 1);
  player->device[sizeof(player->device) - 1] = '\0';

  fprintf(stderr, "doubao: player opened dev=%s rate=%lu ch=%u bps=%u\n",
          device, (unsigned long)sample_rate, channels, bits_per_sample);
  *out = player;
  return 0;
}

int voice_player_write(voice_player_t *player, const uint8_t *data,
                       size_t size)
{
  if (!player || !data)
    {
      return -EINVAL;
    }
  if (size == 0 || player->aborted)
    {
      return 0;
    }

  /* 累积到内存,按需倍增扩容(不超过 VP_MAX_CAP)。 */
  if (player->size + size > player->cap)
    {
      size_t ncap = player->cap;
      uint8_t *nb;
      while (ncap < player->size + size && ncap < VP_MAX_CAP)
        {
          ncap *= 2;
        }
      if (ncap > VP_MAX_CAP)
        {
          ncap = VP_MAX_CAP;
        }
      if (player->size + size > ncap)
        {
          /* 超上限:丢弃多出的音频尾部(极长回复,罕见) */
          size = ncap > player->size ? ncap - player->size : 0;
          if (size == 0)
            {
              return 0;
            }
        }
      nb = realloc(player->data, ncap);
      if (!nb)
        {
          return -ENOMEM;
        }
      player->data = nb;
      player->cap  = ncap;
    }

  memcpy(player->data + player->size, data, size);
  player->size += size;
  return 0;
}

void voice_player_close(voice_player_t *player)
{
  if (!player)
    {
      return;
    }
  /* 收齐的整段 TTS 一次性播完(非 abort 且有数据)。 */
  if (!player->aborted && player->size > 0)
    {
      (void)vp_play(player);
    }
  vp_free(player);
}

void voice_player_abort(voice_player_t *player)
{
  if (!player)
    {
      return;
    }
  /* 丢弃累积数据,不播放,立即释放。 */
  player->aborted = 1;
  vp_free(player);
}
