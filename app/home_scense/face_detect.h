/****************************************************************************
 * app/home_scense/face_detect.h
 * Background frontal-face detection worker for home_scense.
 *
 * Runs BlazeFace (TFLite Micro) on the UVC camera in a dedicated thread,
 * WITHOUT any LCD preview.  When a frontal face is stably present it opens
 * the Doubao full-duplex conversation (doubao_voice_start); when the face
 * is stably gone it closes it (doubao_voice_stop).  Edge-triggered with
 * hysteresis so brief look-aways or per-frame jitter do not flap the
 * conversation.
 *
 * The feature is gated on already-enabled Kconfig symbols so it introduces
 * NO new config option (a new symbol would regenerate .config and force a
 * full rebuild).  It is active when the Doubao voice assistant, TFLite Micro
 * and the video/camera stack are all present.
 ****************************************************************************/

#ifndef HOME_SCENSE_FACE_DETECT_H
#define HOME_SCENSE_FACE_DETECT_H

#include <nuttx/config.h>

#if defined(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE) && \
    defined(CONFIG_TFLITEMICRO) && defined(CONFIG_DRIVERS_VIDEO)
#  define HOME_SCENSE_FACE_DETECT_ENABLED 1
#endif

#ifdef HOME_SCENSE_FACE_DETECT_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

/* Start the background face-detection thread.  Returns 0 on success, or a
 * negative errno.  Safe to call once from main() after doubao_voice_init().
 */
int face_detect_start(void);

/* Signal the worker to stop and join it.  Safe to call from main() teardown. */
void face_detect_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HOME_SCENSE_FACE_DETECT_ENABLED */

#endif /* HOME_SCENSE_FACE_DETECT_H */
