# 公共树源码 overlay

本目录存放**队伍对 openvela 公共仓(nuttx / apps / vendor)的全部源码改动**。
它们不以 commit/PR 形式进入公共仓,而是由本仓 `build.sh` 在**编译前拷贝**到工作区
对应路径(见 `build.sh` 的 `apply_overlay`)。这样"改动的真身住在队伍仓里,公共仓零提交"。

> 该做法沿用 `build.sh` 既有模式(它本就把 `configs/rcS.nsh`、`sys_partition.fex`、
> CA 证书临时拷进 vendor 板);已与组委会确认可行。

## 目录约定

overlay 下按**工作区根目录相对路径**组织,`apply_overlay` 原样拷贝:

```
overlay/nuttx/...              → <workspace>/nuttx/...
overlay/apps/...               → <workspace>/apps/...
overlay/vendor/allwinnertech/… → <workspace>/vendor/allwinnertech/…
```

## 内容(51 个文件)

| 区域 | 文件 | 说明 |
|------|------|------|
| nuttx (7) | `drivers/lcd/ili9341.c` | ILI9341 `putarea`(整帧刷新提速) |
| | `drivers/usbhost/{usbhost_uvc.c,Kconfig,Make.defs,CMakeLists.txt}` | UVC 主机类驱动 + 构建接线 |
| | `include/nuttx/usb/{uvc.h,usbhost_uvc.h}` | UVC 定义 |
| apps (25) | `examples/uvc_test/` | UVC 摄像头预览(含 tjpgd) |
| | `examples/person_detection/` | TFLite Micro 人形检测 |
| | `examples/face_detection/` | BlazeFace 正脸检测 |
| vendor (19) | `chips/r528/drivers/rtos-hal/hal/source/usb/uhc/*` | R528 EHCI USB 主机驱动(11) |
| | `chips/.../usb/{CMakeLists.txt,Kconfig,Make.defs,platform/sun20iw1/*}` | USB HAL 构建接入 |
| | `chips/r528/drv/spi/drv_spi.c` | ILI9341 SPI 时钟 40→60MHz |
| | `boards/r528/r528s3-*/src/r528_bringup.c` | USB host 初始化 |

> **defconfig 不在 overlay 里**:功能开关直接写进 `board/contest_board/configs/nsh/defconfig`
> (build 实际使用的那份),`CONFIG_VIDEO_STREAM` 由 `USBHOST_UVC` 的 `select` 自动开启,
> `CONFIG_ARM_NEON` 由 arch 默认开启。

## 基线(捕获时的上游 dev-ai-contest-2026 提交)

overlay 内的"修改类"文件是在以下公共仓提交之上捕获的快照。`repo sync` 后若上游这些
文件有更新,**直接拷贝会覆盖上游改动**——升级基线时请重新核对/重建 overlay:

| 仓库 | 基线 commit |
|------|-------------|
| nuttx | `dd92bcf` |
| apps (nuttx-apps) | `dcc6a95` |
| vendor/allwinnertech | `16763861` |

## 使用

在 openvela 工作区根目录(队伍仓上一级):

```bash
./contest2026_106_VelaGoGoGo/build.sh full   # 首次/改 defconfig 后:apply_overlay → configure → 编译
./contest2026_106_VelaGoGoGo/build.sh        # 增量:apply_overlay → make
```
