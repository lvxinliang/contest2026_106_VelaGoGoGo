/****************************************************************************
 * app/home_scense/ac_ir_control.h
 * 空调红外(CIR)发射/学习封装 —— 走 NuttX LIRC 设备 /dev/lirc0。
 *
 * 硬件: 全志 R528 专用 CIR TX(GPIO_PB0 复用) → S9013 → 红外发射管;
 *       IRM-3638T 38kHz 接收头 → CIR RX。板级 defconfig 已开:
 *       CONFIG_DRIVERS_CIR_TX=y / CONFIG_DRIVERS_CIR_RX=y / CONFIG_DRIVERS_RC=y
 *
 * 数据格式: LIRC MODE2 —— 每个 32-bit 字用 LIRC_PULSE(us)/LIRC_SPACE(us)
 *           编码(bit24=电平, 低24位=微秒)。发送缓冲个数须为奇数(脉冲开头
 *           脉冲结尾),底层硬件缓冲上限 IR_TX_RAW_BUF_SIZE=256 项。
 ****************************************************************************/

#ifndef HOME_SCENSE_AC_IR_CONTROL_H
#define HOME_SCENSE_AC_IR_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* LIRC 字符设备路径(drv_ir.c 里 R528_IR_NO=0 → /dev/lirc0)。 */
#ifndef AC_IR_DEVPATH
#define AC_IR_DEVPATH "/dev/lirc0"
#endif

/* 空调遥控主流为 38kHz 载波、约 1/3 占空比。个别品牌(部分东芝/夏普)用
 * 33~40kHz,如学习回放不通可在这里微调。 */
#define AC_IR_CARRIER_HZ 38000
#define AC_IR_DUTY_CYCLE 33

/* 学习抓帧的最大脉冲/间隔条数(足够容纳格力/美的等整机状态帧)。 */
#define AC_IR_MAX_WORDS 512

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * 发射一帧原始 LIRC MODE2 波形。
 *   frame : LIRC_PULSE()/LIRC_SPACE() 编码的数组(脉冲开头)
 *   count : 数组元素个数;偶数时自动丢弃末尾(通常是收尾间隔)以满足奇数约束
 * 返回 0 成功, <0 为 -errno。
 ****************************************************************************/
int ac_ir_send_raw(const uint32_t *frame, size_t count);

/* 高层: 发送"开机/关机"帧(帧数据见 ac_ir_frames.h)。
 * 未录入真实帧时返回 -ENODATA 并打日志提示先学习。 */
int ac_ir_power_on(void);
int ac_ir_power_off(void);

/****************************************************************************
 * 从接收头抓取一帧红外波形到 buf(MODE2 字)。
 *   timeout_ms : 首个边沿到来的等待上限;帧内以 ~200ms 静默判定结束。
 * 返回捕获到的字数, <0 为 -errno。
 ****************************************************************************/
int ac_ir_capture(uint32_t *buf, size_t cap, int timeout_ms);

/* 抓一帧并把可直接粘贴进 ac_ir_frames.h 的 C 数组打到 stdout+syslog。
 * label 为数组名(如 "ac_frame_power_on")。返回字数或 <0。 */
int ac_ir_learn_dump(const char *label, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HOME_SCENSE_AC_IR_CONTROL_H */
