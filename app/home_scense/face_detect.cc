/****************************************************************************
 * app/home_scense/face_detect.cc
 *
 * Background frontal-face detection for home_scense.
 *
 * The camera capture / MJPEG decode / BlazeFace inference / frontal-face
 * geometry test are ported verbatim from apps/examples/face_detection.  The
 * differences here:
 *
 *   1. No LCD preview.  All framebuffer / drawing / font code is dropped, so
 *      it never touches /dev/lcd0 and never contends with the LVGL UI.
 *   2. Runs as a pthread worker rather than a standalone main().
 *   3. Instead of printing, it drives the Doubao full-duplex conversation:
 *      a stably-present frontal face calls doubao_voice_start(); a stably-
 *      absent face calls doubao_voice_stop().  Edge-triggered with hysteresis
 *      so per-frame jitter / brief look-aways do not flap the session.
 ****************************************************************************/

#include <nuttx/config.h>

#include "face_detect.h"

#ifdef HOME_SCENSE_FACE_DETECT_ENABLED

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <syslog.h>

#include <nuttx/video/video.h>
#include <nuttx/video/v4l2_cap.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "face_detect_model_data.inc"
#include "tjpgd.h"

/* doubao_voice.h has no extern "C" guard and its definitions live in the C
 * file doubao_voice.c, so include it with C linkage to match the symbols. */
extern "C" {
#include "doubao/doubao_voice.h"
}

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Model input geometry (BlazeFace short-range) */

#define IN_W          128
#define IN_H          128
#define IN_CH         3

/* BlazeFace SSD head geometry (short-range 128x128 model) */

#define NUM_ANCHORS   896
#define NUM_COORDS    16     /* 4 box + 6 keypoints * 2 */
#define NUM_KP        6
#define BOX_SCALE     128.0f

/* Detection thresholds (tunable) */

#define SCORE_THRESH  0.6f
#define NMS_IOU       0.30f
#define MAX_CAND      128
#define MAX_DET       16

/* Frontal-face geometry thresholds (relative to eye distance) */

#define ROLL_THRESH   0.40f
#define YAW_THRESH    0.35f

/* Camera-mount rotation compensation applied to the model input.
 * Values: 0, 90, 180, 270 (degrees clockwise).  See the face_detection
 * example for the rationale.  Overridable from Kconfig.
 */

#ifndef FACE_ROTATE
#  define FACE_ROTATE 270
#endif

#define KP_EYE0       0
#define KP_EYE1       1
#define KP_NOSE       2
#define KP_MOUTH      3

#define TENSOR_ARENA_SIZE (512 * 1024)

#define VIDEO_DEV_PATH "/dev/video"

#define TEST_WIDTH     320
#define TEST_HEIGHT    240
#define TEST_BUF_COUNT 6

/* Hysteresis / pacing (tunable).
 *  - START after this many consecutive frontal frames.
 *  - STOP  after this many consecutive frames with no frontal face.
 *  - sleep between inferences to cap frame rate and yield CPU to UI/audio.
 */

#define PRESENT_FRAMES_TO_START  2
#define ABSENT_FRAMES_TO_STOP    3

/* Inference pacing.  Idle: ~3 fps for a responsive wake.  Talking: keep
 * polling briskly so "face left -> exit" stays responsive (exit latency ~=
 * ABSENT_FRAMES_TO_STOP * FRAME_INTERVAL_TALK_US, here ~1.2s). The USB-driver
 * fix (lowered UVC prio + yielding ISO poll) means this inference no longer
 * starves the audio path, so we no longer need the old 800ms throttle. */
#define FRAME_INTERVAL_IDLE_US   (300 * 1000)
#define FRAME_INTERVAL_TALK_US   (400 * 1000)

/* Worker runs well below the UI/audio/network so heavy TFLite inference never
 * preempts the audio driver's message path (which hangs the vendor codec).
 * NuttX: higher number = higher priority; default task priority is 100. */
#define FACE_DETECT_THREAD_PRIO  50

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct v_buffer
{
  uint8_t *start;
  uint32_t length;
};

struct detection_s
{
  float score;
  float xmin, ymin, xmax, ymax;
  float kp[NUM_KP][2];
  int   frontal;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t  g_tensor_arena[TENSOR_ARENA_SIZE] __attribute__((aligned(16)));
static uint8_t  g_framecopy[TEST_WIDTH * TEST_HEIGHT * 2];
static float    g_inbuf[IN_W * IN_H * IN_CH];   /* 128x128x3 float, [-1,1] */

static float    g_anchor_cx[NUM_ANCHORS];
static float    g_anchor_cy[NUM_ANCHORS];

static detection_s g_cand[MAX_CAND];
static detection_s g_det[MAX_DET];

/* Decode callback context: fills g_inbuf (rotated to upright). */

static float   *g_inf_rgb  = nullptr;
static uint32_t g_inf_srcw = 0;
static uint32_t g_inf_srch = 0;

/* Worker thread control */

static pthread_t     g_thread;
static volatile bool g_running = false;

/****************************************************************************
 * tjpgd callbacks
 ****************************************************************************/

struct mjpeg_iodev_s
{
  const uint8_t *data;
  size_t         len;
  size_t         pos;
};

static size_t mjpeg_input(JDEC *jd, uint8_t *buff, size_t nbyte)
{
  struct mjpeg_iodev_s *io = (struct mjpeg_iodev_s *)jd->device;
  size_t avail = io->len - io->pos;
  if (nbyte > avail) nbyte = avail;
  if (buff) memcpy(buff, io->data + io->pos, nbyte);
  io->pos += nbyte;
  return nbyte;
}

static inline float normalise_u8(uint8_t v)
{
  return (float)v / 127.5f - 1.0f;
}

/* Decode-time: fill only the 128x128 model input (no RGB565 preview). */

static int mjpeg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
  uint8_t *src = (uint8_t *)bitmap;
  uint32_t x;
  uint32_t y;

  (void)jd;

  if (!(g_inf_rgb && g_inf_srcw > 0 && g_inf_srch > 0))
    return 1;

  for (y = rect->top; y <= rect->bottom && y < TEST_HEIGHT; y++)
    {
      for (x = rect->left; x <= rect->right && x < TEST_WIDTH; x++)
        {
          uint8_t b = *src++;
          uint8_t g = *src++;
          uint8_t r = *src++;

#if FACE_ROTATE == 90
          int dx = (int)((g_inf_srch - 1 - y) * IN_W / g_inf_srch);
          int dy = (int)(x * IN_H / g_inf_srcw);
#elif FACE_ROTATE == 180
          int dx = (int)((g_inf_srcw - 1 - x) * IN_W / g_inf_srcw);
          int dy = (int)((g_inf_srch - 1 - y) * IN_H / g_inf_srch);
#elif FACE_ROTATE == 270
          int dx = (int)(y * IN_W / g_inf_srch);
          int dy = (int)((g_inf_srcw - 1 - x) * IN_H / g_inf_srcw);
#else
          int dx = (int)(x * IN_W / g_inf_srcw);
          int dy = (int)(y * IN_H / g_inf_srch);
#endif
          if (dx >= IN_W) dx = IN_W - 1;
          if (dy >= IN_H) dy = IN_H - 1;
          if (dx < 0) dx = 0;
          if (dy < 0) dy = 0;
          float *p = &g_inf_rgb[(dy * IN_W + dx) * IN_CH];
          p[0] = normalise_u8(r);
          p[1] = normalise_u8(g);
          p[2] = normalise_u8(b);
        }

      if (rect->right >= TEST_WIDTH)
        src += (rect->right - TEST_WIDTH + 1) * 3;
    }

  return 1;
}

/****************************************************************************
 * BlazeFace anchor generation (short-range 128x128, 896 anchors)
 ****************************************************************************/

static int generate_anchors(void)
{
  static const int strides[4] = {8, 16, 16, 16};
  const int num_layers = 4;
  const float off = 0.5f;
  int count = 0;
  int layer = 0;

  while (layer < num_layers)
    {
      int last = layer;
      int anchors_per_cell = 0;
      while (last < num_layers && strides[last] == strides[layer])
        {
          anchors_per_cell += 2;
          last++;
        }

      int stride = strides[layer];
      int fm_w = (IN_W + stride - 1) / stride;
      int fm_h = (IN_H + stride - 1) / stride;

      for (int y = 0; y < fm_h; y++)
        for (int x = 0; x < fm_w; x++)
          for (int a = 0; a < anchors_per_cell; a++)
            {
              if (count < NUM_ANCHORS)
                {
                  g_anchor_cx[count] = ((float)x + off) / (float)fm_w;
                  g_anchor_cy[count] = ((float)y + off) / (float)fm_h;
                  count++;
                }
            }

      layer = last;
    }

  return count;
}

/****************************************************************************
 * Post-processing
 ****************************************************************************/

static inline float sigmoidf(float x)
{
  if (x < -100.0f) x = -100.0f;
  if (x >  100.0f) x =  100.0f;
  return 1.0f / (1.0f + expf(-x));
}

static float iou(const detection_s *a, const detection_s *b)
{
  float x0 = fmaxf(a->xmin, b->xmin);
  float y0 = fmaxf(a->ymin, b->ymin);
  float x1 = fminf(a->xmax, b->xmax);
  float y1 = fminf(a->ymax, b->ymax);
  float iw = fmaxf(0.0f, x1 - x0);
  float ih = fmaxf(0.0f, y1 - y0);
  float inter = iw * ih;
  float ua = (a->xmax - a->xmin) * (a->ymax - a->ymin) +
             (b->xmax - b->xmin) * (b->ymax - b->ymin) - inter;
  return ua > 0.0f ? inter / ua : 0.0f;
}

static int decode_detections(const float *box_t, const float *score_t)
{
  int n = 0;

  for (int i = 0; i < NUM_ANCHORS && n < MAX_CAND; i++)
    {
      float score = sigmoidf(score_t[i]);
      if (score < SCORE_THRESH)
        continue;

      const int base = i * NUM_COORDS;
      float acx = g_anchor_cx[i];
      float acy = g_anchor_cy[i];

      float xc = box_t[base + 0] / BOX_SCALE + acx;
      float yc = box_t[base + 1] / BOX_SCALE + acy;
      float w  = box_t[base + 2] / BOX_SCALE;
      float h  = box_t[base + 3] / BOX_SCALE;

      detection_s *d = &g_cand[n++];
      d->score = score;
      d->xmin = xc - w * 0.5f;
      d->ymin = yc - h * 0.5f;
      d->xmax = xc + w * 0.5f;
      d->ymax = yc + h * 0.5f;
      d->frontal = 0;

      for (int k = 0; k < NUM_KP; k++)
        {
          int off = base + 4 + k * 2;
          d->kp[k][0] = box_t[off + 0] / BOX_SCALE + acx;
          d->kp[k][1] = box_t[off + 1] / BOX_SCALE + acy;
        }
    }

  return n;
}

static int nms(int ncand)
{
  static uint8_t removed[MAX_CAND];
  memset(removed, 0, sizeof(removed));
  int ndet = 0;

  for (int iter = 0; iter < ncand && ndet < MAX_DET; iter++)
    {
      int best = -1;
      float best_score = -1.0f;
      for (int i = 0; i < ncand; i++)
        if (!removed[i] && g_cand[i].score > best_score)
          {
            best_score = g_cand[i].score;
            best = i;
          }

      if (best < 0)
        break;

      removed[best] = 1;
      g_det[ndet++] = g_cand[best];

      for (int i = 0; i < ncand; i++)
        if (!removed[i] && iou(&g_cand[best], &g_cand[i]) > NMS_IOU)
          removed[i] = 1;
    }

  return ndet;
}

static int is_frontal(const detection_s *d)
{
  float e0x = d->kp[KP_EYE0][0], e0y = d->kp[KP_EYE0][1];
  float e1x = d->kp[KP_EYE1][0], e1y = d->kp[KP_EYE1][1];
  float nx  = d->kp[KP_NOSE][0], ny  = d->kp[KP_NOSE][1];
  float my  = d->kp[KP_MOUTH][1];

  float dx = e1x - e0x;
  float dy = e1y - e0y;
  float eye_dist = sqrtf(dx * dx + dy * dy);
  if (eye_dist < 1e-4f)
    return 0;

  float eye_mid_x = (e0x + e1x) * 0.5f;
  float eye_mid_y = (e0y + e1y) * 0.5f;

  float roll = fabsf(dy) / eye_dist;
  float yaw  = fabsf(nx - eye_mid_x) / eye_dist;
  int vertical_ok = (ny > eye_mid_y) && (my > ny);

  return (roll < ROLL_THRESH) && (yaw < YAW_THRESH) && vertical_ok;
}

/****************************************************************************
 * Locate box (last dim 16) and score (last dim 1) output tensors
 ****************************************************************************/

static int identify_outputs(tflite::MicroInterpreter *interp,
                            TfLiteTensor **box, TfLiteTensor **score)
{
  *box = nullptr;
  *score = nullptr;
  size_t count = interp->outputs_size();

  for (size_t i = 0; i < count; i++)
    {
      TfLiteTensor *t = interp->output(i);
      int last = t->dims->data[t->dims->size - 1];
      if (last == NUM_COORDS) *box = t;
      else if (last == 1)     *score = t;
    }

  return (*box && *score) ? 0 : -1;
}

/* Run one inference on the staged g_inbuf; returns 1 if any frontal face. */

static int infer_any_frontal(tflite::MicroInterpreter *interp,
                             TfLiteTensor *input, TfLiteTensor *box,
                             TfLiteTensor *score)
{
  memcpy(input->data.f, g_inbuf, sizeof(g_inbuf));

  if (interp->Invoke() != kTfLiteOk)
    return 0;

  int ncand = decode_detections(box->data.f, score->data.f);
  int ndet = nms(ncand);

  for (int i = 0; i < ndet; i++)
    if (is_frontal(&g_det[i]))
      return 1;

  return 0;
}

/****************************************************************************
 * Conversation gating with hysteresis (edge-triggered).
 *
 * We keep our own "conversation opened by face" flag so we only issue
 * start/stop on our own state transitions, never per frame (stop() aborts
 * TTS playback, so it must not be spammed).  The manual UI button is left
 * untouched; if a user manually stops while present, we will not re-open
 * until an absent->present transition happens again.
 ****************************************************************************/

static void gate_conversation(int any_frontal)
{
  static int  present_run = 0;   /* consecutive frontal frames  */
  static int  absent_run  = 0;   /* consecutive no-frontal frames */
  static bool face_opened = false; /* did WE open the session?    */

  if (any_frontal)
    {
      present_run++;
      absent_run = 0;
    }
  else
    {
      absent_run++;
      present_run = 0;
    }

  if (!face_opened && present_run >= PRESENT_FRAMES_TO_START)
    {
      if (doubao_voice_start() == 0)
        {
          face_opened = true;
          syslog(LOG_INFO, "[face_detect] frontal face -> start conversation\n");
        }
    }
  else if (face_opened && absent_run >= ABSENT_FRAMES_TO_STOP)
    {
      doubao_voice_stop();
      face_opened = false;
      syslog(LOG_INFO, "[face_detect] face gone -> stop conversation\n");
    }
}

/****************************************************************************
 * Worker thread
 ****************************************************************************/

static void *face_detect_worker(void *arg)
{
  (void)arg;

  /* Self-lower priority from inside the thread (more reliable than the attr,
   * which some NuttX configs ignore) so heavy inference never preempts the
   * audio/UI path. */
  (void)pthread_setschedprio(pthread_self(), FACE_DETECT_THREAD_PRIO);

  const tflite::Model *model = nullptr;
  TfLiteTensor *input = nullptr;
  TfLiteTensor *box = nullptr;
  TfLiteTensor *score = nullptr;

  struct v4l2_format fmt;
  struct v4l2_requestbuffers req;
  struct v4l2_buffer buf;
  struct v_buffer *buffers = nullptr;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int fd = -1;
  int ret;
  int i;

  tflite::InitializeTarget();

  model = tflite::GetModel(g_face_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION)
    {
      syslog(LOG_ERR, "[face_detect] model version mismatch\n");
      return nullptr;
    }

  tflite::MicroMutableOpResolver<14> op_resolver;
  op_resolver.AddConv2D();
  op_resolver.AddDepthwiseConv2D();
  op_resolver.AddMaxPool2D();
  op_resolver.AddAveragePool2D();
  op_resolver.AddAdd();
  op_resolver.AddPad();
  op_resolver.AddReshape();
  op_resolver.AddConcatenation();
  op_resolver.AddLogistic();
  op_resolver.AddQuantize();
  op_resolver.AddDequantize();

  tflite::MicroInterpreter interpreter(
      model, op_resolver, g_tensor_arena, TENSOR_ARENA_SIZE);

  if (interpreter.AllocateTensors() != kTfLiteOk)
    {
      syslog(LOG_ERR, "[face_detect] AllocateTensors failed\n");
      return nullptr;
    }

  input = interpreter.input(0);
  if (identify_outputs(&interpreter, &box, &score) != 0)
    {
      syslog(LOG_ERR, "[face_detect] output tensors not found\n");
      return nullptr;
    }

  generate_anchors();
  syslog(LOG_INFO, "[face_detect] model ready, arena %zu/%d\n",
         interpreter.arena_used_bytes(), TENSOR_ARENA_SIZE);

  /* Camera bring-up (identical to the face_detection example). */

  ret = capture_initialize(VIDEO_DEV_PATH);
  if (ret != 0 && ret != -EEXIST)
    {
      syslog(LOG_ERR, "[face_detect] capture_initialize failed: %d\n", ret);
      return nullptr;
    }

  fd = open(VIDEO_DEV_PATH, O_RDWR);
  if (fd < 0)
    {
      syslog(LOG_ERR, "[face_detect] open %s failed: %d\n",
             VIDEO_DEV_PATH, errno);
      return nullptr;
    }

  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = TEST_WIDTH;
  fmt.fmt.pix.height = TEST_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  ret = ioctl(fd, VIDIOC_S_FMT, (uintptr_t)&fmt);
  if (ret < 0)
    { syslog(LOG_ERR, "[face_detect] S_FMT failed: %d\n", errno); goto out; }

  if (fmt.fmt.pix.sizeimage == 0)
    fmt.fmt.pix.sizeimage = TEST_WIDTH * TEST_HEIGHT * 2;

  memset(&req, 0, sizeof(req));
  req.count = TEST_BUF_COUNT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;

  ret = ioctl(fd, VIDIOC_REQBUFS, (uintptr_t)&req);
  if (ret < 0)
    { syslog(LOG_ERR, "[face_detect] REQBUFS failed: %d\n", errno); goto out; }

  buffers = (struct v_buffer *)malloc(req.count * sizeof(struct v_buffer));
  if (!buffers)
    goto out;
  memset(buffers, 0, req.count * sizeof(struct v_buffer));

  for (i = 0; i < (int)req.count; i++)
    {
      buffers[i].length = fmt.fmt.pix.sizeimage;
      buffers[i].start = (uint8_t *)memalign(64, buffers[i].length);
      if (!buffers[i].start)
        { syslog(LOG_ERR, "[face_detect] memalign failed\n"); goto out; }

      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_USERPTR;
      buf.index = i;
      buf.m.userptr = (uintptr_t)buffers[i].start;
      buf.length = buffers[i].length;

      ret = ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buf);
      if (ret < 0)
        syslog(LOG_ERR, "[face_detect] QBUF %d failed: %d\n", i, errno);
    }

  ret = ioctl(fd, VIDIOC_STREAMON, (uintptr_t)&type);
  if (ret < 0)
    { syslog(LOG_ERR, "[face_detect] STREAMON failed: %d\n", errno); goto out; }

  g_inf_rgb  = g_inbuf;
  g_inf_srcw = TEST_WIDTH;
  g_inf_srch = TEST_HEIGHT;

  syslog(LOG_INFO, "[face_detect] streaming, entering detect loop\n");

  while (g_running)
    {
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_USERPTR;

      ret = ioctl(fd, VIDIOC_DQBUF, (uintptr_t)&buf);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[face_detect] DQBUF failed: %d\n", errno);
          usleep(FRAME_INTERVAL_IDLE_US);
          continue;
        }

      uint32_t n = buf.bytesused;
      if (n > sizeof(g_framecopy))
        n = sizeof(g_framecopy);
      memcpy(g_framecopy, (const void *)buf.m.userptr, n);

      ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buf);

      if (n > 1024)
        {
          static uint8_t jdwork[4096];
          JDEC jdec;
          JRESULT res;
          struct mjpeg_iodev_s iodev;

          iodev.data = g_framecopy;
          iodev.len  = n;
          iodev.pos  = 0;

          memset(g_inbuf, 0, sizeof(g_inbuf));

          res = jd_prepare(&jdec, mjpeg_input, jdwork, sizeof(jdwork), &iodev);
          if (res == JDR_OK)
            {
              jd_decomp(&jdec, mjpeg_output, 0);

              int any_frontal = infer_any_frontal(&interpreter, input,
                                                  box, score);
              gate_conversation(any_frontal);
            }
        }

      /* Pace inference. While a conversation is active we only need to notice
       * the face leaving, so slow right down to shed CPU and keep well clear
       * of the audio path; when idle, run faster for a responsive wake. */
      usleep(doubao_voice_is_talking() ? FRAME_INTERVAL_TALK_US
                                       : FRAME_INTERVAL_IDLE_US);
    }

  ioctl(fd, VIDIOC_STREAMOFF, (uintptr_t)&type);

out:
  if (buffers)
    {
      for (i = 0; i < (int)req.count; i++)
        if (buffers[i].start) free(buffers[i].start);
      free(buffers);
    }
  if (fd >= 0)
    close(fd);
  syslog(LOG_INFO, "[face_detect] worker exit\n");
  return nullptr;
}

/****************************************************************************
 * Public API
 ****************************************************************************/

int face_detect_start(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  int ret;

  if (g_running)
    return -EALREADY;

  pthread_attr_init(&attr);

  /* Inference buffers are static globals, so a modest stack suffices; give
   * headroom for the TFLite call chain. */
  pthread_attr_setstacksize(&attr, 128 * 1024);

  /* Force an explicit LOW priority (not inherited from the caller, which runs
   * at the default 100).  Confirmed on-device: our decode+inference CPU load
   * concurrent with audio recording starves the vendor audio driver's message
   * path and hangs it.  Running strictly below the audio/UI threads lets them
   * preempt us so audio never starves; face detection just uses spare CPU. */
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  param.sched_priority = FACE_DETECT_THREAD_PRIO;
  pthread_attr_setschedparam(&attr, &param);

  g_running = true;
  ret = pthread_create(&g_thread, &attr, face_detect_worker, nullptr);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      g_running = false;
      syslog(LOG_ERR, "[face_detect] pthread_create failed: %d\n", ret);
      return -ret;
    }

  return 0;
}

void face_detect_stop(void)
{
  if (!g_running)
    return;

  g_running = false;
  pthread_join(g_thread, nullptr);
}

#endif /* HOME_SCENSE_FACE_DETECT_ENABLED */
