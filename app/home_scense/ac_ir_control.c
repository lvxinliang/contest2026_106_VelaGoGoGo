/****************************************************************************
 * app/home_scense/ac_ir_control.c
 * 空调红外发射/学习 —— 走 /dev/lirc0 (NuttX LIRC)。
 ****************************************************************************/

#include "ac_ir_control.h"
#include "ac_ir_frames.h"

#include <nuttx/lirc.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define AC_IR_LOG(fmt, ...) syslog(LOG_INFO, "[ac_ir] " fmt "\n", ##__VA_ARGS__)
#define AC_IR_ERR(fmt, ...) syslog(LOG_ERR,  "[ac_ir] " fmt "\n", ##__VA_ARGS__)

/* 一条指令重复发送次数与遍间间隔(ms)。实测美的空调偶尔需按多次才生效,
 * 遥控器本身也连发, 故默认多发几遍提高命中率。 */
#ifndef AC_IR_TX_REPEATS
#define AC_IR_TX_REPEATS 10
#endif
#ifndef AC_IR_TX_GAP_MS
#define AC_IR_TX_GAP_MS 40
#endif

/****************************************************************************
 * 发射一帧原始波形
 ****************************************************************************/

int ac_ir_send_raw(const uint32_t *frame, size_t count)
{
  int fd;
  ssize_t nw;
  size_t bytes;

  if (frame == NULL || count == 0)
    {
      return -EINVAL;
    }

  /* LIRC 要求发送缓冲为奇数个(脉冲开头、脉冲结尾)。若抓帧时多录了收尾的
   * 静默间隔导致偶数,丢弃末尾一项即可。 */
  if ((count & 1) == 0)
    {
      count -= 1;
    }

  fd = open(AC_IR_DEVPATH, O_RDWR);
  if (fd < 0)
    {
      AC_IR_ERR("open %s failed: %d", AC_IR_DEVPATH, errno);
      return -errno;
    }

  /* ioctl 参数按值传递(见 drivers/rc/lirc_dev.c)。失败不致命:部分底层对
   * 载波/占空比用默认值,仍能发射。 */
  (void)ioctl(fd, LIRC_SET_SEND_MODE, LIRC_MODE_PULSE);
  (void)ioctl(fd, LIRC_SET_SEND_CARRIER, AC_IR_CARRIER_HZ);
  (void)ioctl(fd, LIRC_SET_SEND_DUTY_CYCLE, AC_IR_DUTY_CYCLE);

  bytes = count * sizeof(uint32_t);
  nw = write(fd, frame, bytes);
  close(fd);

  if (nw < 0)
    {
      AC_IR_ERR("write failed: %d", errno);
      return -errno;
    }

  AC_IR_LOG("sent %zu words (%zd bytes) @ %dHz", count, nw, AC_IR_CARRIER_HZ);
  return 0;
}

/* 重复发送同一帧 AC_IR_TX_REPEATS 遍, 遍间隔 AC_IR_TX_GAP_MS 毫秒, 提高
 * 命中率。任一遍成功即算成功; 全部失败返回最后一次的错误码。 */
static int ac_ir_send_repeat(const uint32_t *frame, size_t count)
{
  int ok = 0;
  int last = -EIO;
  int i;

  for (i = 0; i < AC_IR_TX_REPEATS; i++)
    {
      last = ac_ir_send_raw(frame, count);
      if (last == 0)
        {
          ok++;
        }
      if (i + 1 < AC_IR_TX_REPEATS)
        {
          usleep(AC_IR_TX_GAP_MS * 1000);
        }
    }

  AC_IR_LOG("repeat done: %d/%d 遍成功", ok, AC_IR_TX_REPEATS);
  return ok > 0 ? 0 : last;
}

/****************************************************************************
 * 高层开/关
 ****************************************************************************/

int ac_ir_power_on(void)
{
#if AC_IR_HAVE_ON
  return ac_ir_send_repeat(g_ac_frame_power_on,
                           sizeof(g_ac_frame_power_on) / sizeof(uint32_t));
#else
  AC_IR_ERR("power_on: 尚未录入开机帧。请先『学习空调红外』并把数组填入"
            " ac_ir_frames.h,再将 AC_IR_HAVE_ON 置 1。");
  return -ENODATA;
#endif
}

int ac_ir_power_off(void)
{
#if AC_IR_HAVE_OFF
  return ac_ir_send_repeat(g_ac_frame_power_off,
                           sizeof(g_ac_frame_power_off) / sizeof(uint32_t));
#else
  AC_IR_ERR("power_off: 尚未录入关机帧,见 ac_ir_frames.h 说明。");
  return -ENODATA;
#endif
}

/****************************************************************************
 * 学习: 从接收头抓一帧
 *
 * ★ 关键: R528 的 CIR RX 底层(drv_ir.c r528_cir_callback)把全志原始 FIFO
 *   字节直接透传给 lirc_sample_event —— 每个字节 = bit7(电平) + 低7位(游程
 *   计数),而非 LIRC 微秒格式。采样时钟 = HOSC24M(24MHz)/256 = 93.75kHz,
 *   即每计数 ≈ 10.667µs (= cnt*32/3)。因此必须:
 *     ① 取 bit7 为电平、低7位为计数;
 *     ② 合并相邻同电平字节(一段长间隔会被拆成多个 0x7F 饱和值);
 *     ③ 计数 × 采样周期 → 微秒,再编成 LIRC_PULSE/SPACE(与 TX 一致,可回放)。
 *   注: 底层已 signal_invert=true, 约定 bit7=1 为脉冲(mark)。若回放不通,
 *   把 AC_IR_RX_PULSE_LEVEL 改为 0 试反相。
 ****************************************************************************/

/* 采样周期(µs) = 计数 * 32 / 3 ≈ 10.667µs。 */
#define AC_IR_CNT_TO_US(cnt) (((unsigned int)(cnt) * 32u) / 3u)

/* 原始 FIFO 字节里代表"脉冲(mark)"的电平位。 */
#ifndef AC_IR_RX_PULSE_LEVEL
#define AC_IR_RX_PULSE_LEVEL 1
#endif

/* 引导码判据: mark 时长超过该阈值(µs)视为一帧起始(几乎所有空调帧头
 * 都有 2.5~9ms 的 leader mark)。用它裁掉前导垃圾、并按重复帧切分。 */
#ifndef AC_IR_LEADER_MIN_US
#define AC_IR_LEADER_MIN_US 2500
#endif

/* 归一化参数(脉冲距离编码): space 超过阈值判为 bit1, 否则 bit0;
 * 重建时用标准 mark / bit0 / bit1 时序抹平抖动。可按机型微调。 */
#define AC_IR_BIT_SPACE_THRESH_US 1000
#define AC_IR_NORM_MARK_US        500
#define AC_IR_NORM_BIT0_US        600
#define AC_IR_NORM_BIT1_US        1600

/* 多次采集表决: 需要 AGREE 次结果完全一致即判定可靠, 最多尝试 ATTEMPTS 次
 * (无效/噪声帧不计入一致计数, 只消耗尝试次数)。 */
#define AC_IR_LEARN_AGREE    3
#define AC_IR_LEARN_ATTEMPTS 12
#define AC_IR_LEARN_UNIQ_MAX 8

/* 帧有效性校验: 位数下限, 以及帧头 mark/space 的合理区间(µs), 用于拒绝
 * 残留噪声(如 26bit / S3040 之类)。按机型可调。 */
#define AC_IR_MIN_BITS      32
#define AC_IR_HDR_MIN_US    2800
#define AC_IR_HDR_MAX_US    6000

int ac_ir_capture(uint32_t *buf, size_t cap, int timeout_ms)
{
  struct pollfd pfd;
  int fd;
  size_t n = 0;
  int prev_level = -1;      /* 当前合并中的电平, -1=尚未开始 */
  unsigned int acc = 0;     /* 当前电平累计计数 */
  bool leader_seen = false; /* 是否已遇到引导码(帧真正开始) */
  bool stop = false;        /* 命中下一帧引导码 → 已取满一整帧 */

  if (buf == NULL || cap == 0)
    {
      return -EINVAL;
    }

  fd = open(AC_IR_DEVPATH, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      AC_IR_ERR("open %s (rx) failed: %d", AC_IR_DEVPATH, errno);
      return -errno;
    }

  /* 尽力设为 MODE2 原始收码;不支持则忽略。 */
  (void)ioctl(fd, LIRC_SET_REC_MODE, LIRC_MODE_MODE2);

  pfd.fd = fd;
  pfd.events = POLLIN;

  /* 提交一段合并好的游程:
   *  - 遇到 leader mark(长脉冲): 首个 → 清空缓冲从此开始记录(丢前导垃圾);
   *    第二个 → 说明进入重复帧,置 stop 结束(正好取一整帧,不存该 mark)。
   *  - leader 之后的普通 mark/space 才存入;之前的一律丢弃。 */
#define AC_IR_FLUSH_RUN()                                                     \
  do {                                                                        \
      if (prev_level >= 0 && acc > 0)                                         \
        {                                                                     \
          bool is_pulse = (prev_level == AC_IR_RX_PULSE_LEVEL);               \
          unsigned int us = AC_IR_CNT_TO_US(acc);                             \
          if (is_pulse && us >= AC_IR_LEADER_MIN_US)                          \
            {                                                                 \
              if (!leader_seen)                                               \
                {                                                             \
                  leader_seen = true;                                         \
                  n = 0;                                                      \
                  buf[n++] = LIRC_PULSE(us);                                  \
                }                                                             \
              else                                                            \
                {                                                             \
                  stop = true;                                                \
                }                                                             \
            }                                                                 \
          else if (leader_seen && n < cap)                                    \
            {                                                                 \
              buf[n++] = is_pulse ? LIRC_PULSE(us) : LIRC_SPACE(us);          \
            }                                                                 \
        }                                                                     \
  } while (0)

  while (n < cap && !stop)
    {
      int pr;
      uint32_t w;
      ssize_t r;

      pr = poll(&pfd, 1, (!leader_seen) ? timeout_ms : 200);
      if (pr <= 0)
        {
          break;  /* 首帧超时=没收到; 帧后静默 ~200ms = 结束 */
        }

      if (!(pfd.revents & POLLIN))
        {
          continue;
        }

      /* 一次把 FIFO 尽量读干,降低溢出丢样。 */
      while ((r = read(fd, &w, sizeof(w))) == (ssize_t)sizeof(w))
        {
          unsigned int raw = w & 0xffu;   /* 全志原始 FIFO 字节 */
          int level;
          unsigned int cnt;

          /* raw==0 是驱动的空闲/溢出标记(RPE/ROI),不可靠,直接忽略,
           * 帧结束只靠 200ms 静默或下一帧引导码判定。 */
          if (raw == 0)
            {
              continue;
            }

          level = (raw >> 7) & 0x1;
          cnt   = raw & 0x7fu;

          if (level == prev_level)
            {
              acc += cnt;                 /* 同电平: 累加(还原被拆的长段) */
            }
          else
            {
              AC_IR_FLUSH_RUN();           /* 电平翻转: 先提交上一段 */
              prev_level = level;
              acc = cnt;
              if (stop)
                {
                  break;
                }
            }
        }

      if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          break;
        }
    }

  if (!stop)
    {
      AC_IR_FLUSH_RUN();  /* 正常结束: 提交最后一段 */
    }
#undef AC_IR_FLUSH_RUN

  /* 保证奇数个(脉冲开头、脉冲结尾): 末尾若是空间隔则丢弃。 */
  if (n > 0 && (n & 1) == 0)
    {
      n -= 1;
    }

  close(fd);
  return (int)n;
}

/* 一帧解码结果(引导码 + 位序列)。 */
struct ac_ir_pattern_s
{
  int          nbits;
  unsigned int hdr_mark;
  unsigned int hdr_space;
  int          count;                     /* 该模式出现次数 */
  uint8_t      bits[AC_IR_MAX_WORDS / 2];
};

/* 把一次抓取的原始 mode2 数组解码为位序列(脉冲距离编码: bit 在 mark 后的
 * space 里)。成功返回位数, <0 失败。 */
static int ac_ir_decode_bits(const uint32_t *buf, int n,
                             struct ac_ir_pattern_s *p)
{
  int i;
  int nbits = 0;

  if (n < 3)
    {
      return -EINVAL;
    }

  p->hdr_mark  = LIRC_VALUE(buf[0]);
  p->hdr_space = LIRC_VALUE(buf[1]);
  for (i = 2; i + 1 < n; i += 2)
    {
      p->bits[nbits++] = (LIRC_VALUE(buf[i + 1]) > AC_IR_BIT_SPACE_THRESH_US)
                         ? 1 : 0;
    }

  p->nbits = nbits;
  return nbits;
}

/* 美的(Midea)结构自校验: 帧为偶数个字节, 每个奇数字节 = 前一字节按位取反。
 * 通过则该帧几乎必然无丢样(随机损坏极难同时满足多对补码), 可直接采纳。
 * 非美的机型此检查自然不成立, 退回多次表决即可。 */
static bool ac_ir_complement_ok(const uint8_t *bits, int nbits)
{
  int nbytes = nbits / 8;
  int i;

  if (nbits % 8 != 0 || nbytes < 2 || (nbytes & 1) != 0)
    {
      return false;
    }

  for (i = 0; i < nbytes; i += 2)
    {
      unsigned int b0 = 0;
      unsigned int b1 = 0;
      int k;
      for (k = 0; k < 8; k++)
        {
          b0 |= (unsigned int)bits[i * 8 + k] << k;
          b1 |= (unsigned int)bits[(i + 1) * 8 + k] << k;
        }
      if (((b0 ^ b1) & 0xff) != 0xff)
        {
          return false;
        }
    }

  return true;
}

/* 打印位序列的 LSB-first hex(便于比对稳定性)。 */
static void ac_ir_print_hex(const uint8_t *bits, int nbits)
{
  int b;

  for (b = 0; b < nbits; b += 8)
    {
      unsigned int byte = 0;
      int k;
      for (k = 0; k < 8 && (b + k) < nbits; k++)
        {
          byte |= (unsigned int)bits[b + k] << k;
        }
      printf(" %02X", byte & 0xff);
    }
}

int ac_ir_learn_dump(const char *label, int timeout_ms)
{
  static uint32_t buf[AC_IR_MAX_WORDS];
  static struct ac_ir_pattern_s uniq[AC_IR_LEARN_UNIQ_MAX];
  const char *name = (label && label[0]) ? label : "ac_frame";
  int nuniq = 0;
  int chosen = -1;
  int attempt;
  int good = 0;   /* 已计入表决的有效帧数 */
  int b;

  AC_IR_LOG("学习开始: 将遥控器贴近接收头(<5cm)正对, 按提示重复按同一个键, "
            "凑够 %d 次一致即完成...", AC_IR_LEARN_AGREE);

  for (attempt = 0; attempt < AC_IR_LEARN_ATTEMPTS && chosen < 0; attempt++)
    {
      struct ac_ir_pattern_s cur;
      int n;
      int j;

      AC_IR_LOG("第 %d 次采集: 请对准按键...", good + 1);
      n = ac_ir_capture(buf, AC_IR_MAX_WORDS, (attempt == 0) ? timeout_ms
                                                             : 8000);
      if (ac_ir_decode_bits(buf, n, &cur) < 0)
        {
          AC_IR_ERR("  未捕获(n=%d), 重试", n);
          continue;
        }

      /* 有效性校验: 拒绝噪声/残缺帧(位数太少或帧头不合理)。 */
      if (cur.nbits < AC_IR_MIN_BITS ||
          cur.hdr_mark < AC_IR_HDR_MIN_US || cur.hdr_mark > AC_IR_HDR_MAX_US ||
          cur.hdr_space < AC_IR_HDR_MIN_US || cur.hdr_space > AC_IR_HDR_MAX_US)
        {
          AC_IR_ERR("  疑似噪声(M%u/S%u,%dbit), 忽略并重试",
                    cur.hdr_mark, cur.hdr_space, cur.nbits);
          continue;
        }

      good++;
      printf("[ac_ir] #%d leader=M%u/S%u, %d bits:",
             good, cur.hdr_mark, cur.hdr_space, cur.nbits);
      ac_ir_print_hex(cur.bits, cur.nbits);
      printf("%s\n", ac_ir_complement_ok(cur.bits, cur.nbits) ? "  [OK补码]"
                                                              : "");

      /* 通过美的补码自校验的帧几乎必然无丢样, 直接采纳(无需凑够多次一致)。 */
      if (ac_ir_complement_ok(cur.bits, cur.nbits))
        {
          int j2;
          for (j2 = 0; j2 < nuniq; j2++)
            {
              if (uniq[j2].nbits == cur.nbits &&
                  memcmp(uniq[j2].bits, cur.bits, cur.nbits) == 0)
                {
                  break;
                }
            }
          if (j2 == nuniq && nuniq < AC_IR_LEARN_UNIQ_MAX)
            {
              uniq[nuniq++] = cur;
            }
          uniq[j2].count = AC_IR_LEARN_AGREE;  /* 标记为可靠 */
          chosen = j2;
          AC_IR_LOG("该帧通过美的补码校验 → 可靠, 立即采纳。");
          break;
        }

      /* 否则退回多次表决: 位数+位序列全同才算一致。 */
      for (j = 0; j < nuniq; j++)
        {
          if (uniq[j].nbits == cur.nbits &&
              memcmp(uniq[j].bits, cur.bits, cur.nbits) == 0)
            {
              break;
            }
        }

      if (j < nuniq)
        {
          uniq[j].count++;
        }
      else if (nuniq < AC_IR_LEARN_UNIQ_MAX)
        {
          uniq[j] = cur;
          uniq[j].count = 1;
          nuniq++;
        }
      else
        {
          continue;  /* 唯一模式过多(全在丢样),忽略 */
        }

      if (uniq[j].count >= AC_IR_LEARN_AGREE)
        {
          chosen = j;
        }
    }

  /* 未凑够一致次数则取出现最多的一份。 */
  if (chosen < 0)
    {
      int best = -1;
      int bestc = 0;
      int j;
      for (j = 0; j < nuniq; j++)
        {
          if (uniq[j].count > bestc)
            {
              bestc = uniq[j].count;
              best = j;
            }
        }

      if (best < 0)
        {
          AC_IR_ERR("学习失败: 未捕获到有效帧。检查接收头/对准/距离后重试。");
          return -ETIMEDOUT;
        }

      chosen = best;
      AC_IR_ERR("未达成 %d 次一致(最多一份仅 %d 次),可靠性较低, 建议贴近重试。",
                AC_IR_LEARN_AGREE, bestc);
    }
  else
    {
      AC_IR_LOG("已达成 %d 次一致 → 帧稳定可靠。", uniq[chosen].count);
    }

  /* 输出归一化(标准时序)的可粘贴数组。 */
  printf("[ac_ir] FINAL leader=M%u/S%u, %d bits:",
         uniq[chosen].hdr_mark, uniq[chosen].hdr_space, uniq[chosen].nbits);
  ac_ir_print_hex(uniq[chosen].bits, uniq[chosen].nbits);
  printf("\n\n/* ==== normalized: leader + %d bits, paste into "
         "ac_ir_frames.h ==== */\n", uniq[chosen].nbits);
  printf("static const uint32_t %s[] =\n{\n  ", name);
  printf("LIRC_PULSE(%u), LIRC_SPACE(%u),\n  ",
         uniq[chosen].hdr_mark, uniq[chosen].hdr_space);
  for (b = 0; b < uniq[chosen].nbits; b++)
    {
      printf("LIRC_PULSE(%d), LIRC_SPACE(%d),%s",
             AC_IR_NORM_MARK_US,
             uniq[chosen].bits[b] ? AC_IR_NORM_BIT1_US : AC_IR_NORM_BIT0_US,
             ((b + 1) % 3 == 0) ? "\n  " : " ");
    }
  printf("LIRC_PULSE(%d),\n};\n/* ==== end (%d words) ==== */\n\n",
         AC_IR_NORM_MARK_US, uniq[chosen].nbits * 2 + 3);

  AC_IR_LOG("学习完成: %d bits。已打印数组 %s[]。", uniq[chosen].nbits, name);
  return uniq[chosen].nbits;
}
