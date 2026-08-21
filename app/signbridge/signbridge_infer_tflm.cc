/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_infer_tflm.cc
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * TFLite Micro inference wrapper for SignBridge.
 *
 * Architecture:
 *   Camera frame (RGB888) → resize/crop to 192x192 →
 *   Hand landmark model → 21 keypoints (x,y,z) →
 *   Temporal classifier → sign class + confidence
 *
 * Models are loaded from ROMFS at runtime.  When TFLITEMICRO is
 * disabled, this file compiles to stub functions only.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <syslog.h>

#include "signbridge.h"
#include "signbridge_infer.h"

#ifdef CONFIG_TFLITEMICRO

/****************************************************************************
 * TFLite Micro includes
 ****************************************************************************/

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Arena size for the hand landmark model (float16 → ~8 MB) */

#define HAND_LM_ARENA_SIZE   (8 * 1024 * 1024)

/* Arena size for the temporal classifier (small MLP) */

#define CLASSIFIER_ARENA_SIZE (256 * 1024)

/* Model file paths in ROMFS */

#define HAND_LM_MODEL_PATH  "/etc/models/hand_landmarks_detector.tflite"
#define HAND_DET_MODEL_PATH "/etc/models/hand_detector.tflite"

/* Input image dimensions for hand landmark model */

#define HAND_LM_INPUT_W   224
#define HAND_LM_INPUT_H   224

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Hand landmark model */

static uint8_t *g_hand_lm_arena;
static const tflite::Model *g_hand_lm_model;
static tflite::MicroInterpreter *g_hand_lm_interp;

/* Temporal classifier */

static uint8_t *g_classifier_arena;
static const tflite::Model *g_classifier_model;
static tflite::MicroInterpreter *g_classifier_interp;

/* Model file buffers (loaded from ROMFS) */

static uint8_t *g_hand_lm_model_buf;
static size_t   g_hand_lm_model_size;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: load_model_from_file
 *
 * Description:
 *   Load a TFLite model from a ROMFS file path into a malloc'd buffer.
 *
 ****************************************************************************/

static int load_model_from_file(const char *path,
                                uint8_t **out_buf,
                                size_t *out_size)
{
  FILE *fp;
  long file_size;
  uint8_t *buf;
  size_t nread;

  fp = fopen(path, "rb");
  if (fp == NULL)
    {
      syslog(LOG_ERR, "infer: failed to open model: %s\n", path);
      return -errno;
    }

  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (file_size <= 0)
    {
      syslog(LOG_ERR, "infer: empty model file: %s\n", path);
      fclose(fp);
      return -EINVAL;
    }

  buf = (uint8_t *)kmm_malloc((size_t)file_size);
  if (buf == NULL)
    {
      syslog(LOG_ERR, "infer: malloc failed for model (%ld bytes)\n",
             file_size);
      fclose(fp);
      return -ENOMEM;
    }

  nread = fread(buf, 1, (size_t)file_size, fp);
  fclose(fp);

  if (nread != (size_t)file_size)
    {
      syslog(LOG_ERR, "infer: read error: %s (%zu/%ld)\n",
             path, nread, file_size);
      kmm_free(buf);
      return -EIO;
    }

  *out_buf  = buf;
  *out_size = (size_t)file_size;

  syslog(LOG_INFO, "infer: loaded %s (%zu bytes)\n", path, *out_size);
  return OK;
}

/****************************************************************************
 * Name: init_hand_landmark_model
 *
 * Description:
 *   Initialize the hand landmark TFLite Micro interpreter.
 *
 ****************************************************************************/

static int init_hand_landmark_model(void)
{
  int ret;

  /* 1. Load model from ROMFS */

  ret = load_model_from_file(HAND_LM_MODEL_PATH,
                              &g_hand_lm_model_buf,
                              &g_hand_lm_model_size);
  if (ret < 0)
    {
      return ret;
    }

  /* 2. Verify TFLite flatbuffer */

  g_hand_lm_model = tflite::GetModel(g_hand_lm_model_buf);
  if (g_hand_lm_model->version() != TFLITE_SCHEMA_VERSION)
    {
      syslog(LOG_ERR, "infer: model schema version mismatch\n");
      return -EINVAL;
    }

  /* 3. Create op resolver (add only the ops used by the model) */

  static tflite::MicroMutableOpResolver<16> resolver;

  /* Common ops used by MediaPipe hand landmark model */

  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddMaxPool2D();
  resolver.AddAveragePool2D();
  resolver.AddReshape();
  resolver.AddSoftmax();
  resolver.AddQuantize();
  resolver.AddDequantize();
  resolver.AddAdd();
  resolver.AddMul();
  resolver.AddConcatenation();
  resolver.AddLogistic();
  resolver.AddStridedSlice();
  resolver.AddPack();
  resolver.AddCustom();

  /* 4. Allocate arena in PSRAM */

  g_hand_lm_arena = (uint8_t *)kmm_malloc(HAND_LM_ARENA_SIZE);
  if (g_hand_lm_arena == NULL)
    {
      syslog(LOG_ERR, "infer: arena alloc failed (%d bytes)\n",
             HAND_LM_ARENA_SIZE);
      return -ENOMEM;
    }

  /* 5. Create interpreter */

  static tflite::MicroInterpreter static_interp(
      g_hand_lm_model, resolver,
      g_hand_lm_arena, HAND_LM_ARENA_SIZE);

  g_hand_lm_interp = &static_interp;

  TfLiteStatus status = g_hand_lm_interp->AllocateTensors();
  if (status != kTfLiteOk)
    {
      syslog(LOG_ERR, "infer: AllocateTensors failed\n");
      return -ENOMEM;
    }

  syslog(LOG_INFO, "infer: hand landmark model ready (%zu bytes)\n",
         g_hand_lm_model_size);
  return OK;
}

#endif /* CONFIG_TFLITEMICRO */

/****************************************************************************
 * Public Functions (C linkage)
 ****************************************************************************/

extern "C"
{

int signbridge_infer_init(void)
{
#ifdef CONFIG_TFLITEMICRO
  int ret;

  syslog(LOG_INFO, "signbridge: initializing TFLite Micro inference\n");

  ret = init_hand_landmark_model();
  if (ret < 0)
    {
      syslog(LOG_ERR, "signbridge: hand landmark init failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "signbridge: TFLM inference ready\n");
#else
  syslog(LOG_INFO, "signbridge: inference stub mode (no TFLM)\n");
#endif

  return OK;
}

int signbridge_run_hand_landmark(const uint8_t *image,
                                 int width, int height,
                                 struct signbridge_landmark_s *landmarks)
{
#ifdef CONFIG_TFLITEMICRO
  /* TODO: Pre-process image → quantize → invoke → dequantize
   *
   * For now, return stub landmarks until camera integration is done.
   * The model is loaded and the interpreter is ready; the actual
   * inference call will be wired up when the camera pipeline lands.
   */
#endif

  /* Stub: generate fixed "open hand" landmarks for pipeline testing */

  static const float stub[SIGNBRIDGE_NUM_LANDMARKS][3] =
  {
    {0.50f, 0.85f, 0.0f}, {0.42f, 0.70f, 0.0f}, {0.35f, 0.55f, 0.0f},
    {0.30f, 0.45f, 0.0f}, {0.28f, 0.38f, 0.0f}, {0.50f, 0.65f, 0.0f},
    {0.50f, 0.50f, 0.0f}, {0.50f, 0.40f, 0.0f}, {0.50f, 0.32f, 0.0f},
    {0.55f, 0.65f, 0.0f}, {0.55f, 0.48f, 0.0f}, {0.55f, 0.38f, 0.0f},
    {0.55f, 0.30f, 0.0f}, {0.60f, 0.67f, 0.0f}, {0.60f, 0.52f, 0.0f},
    {0.60f, 0.42f, 0.0f}, {0.60f, 0.35f, 0.0f}, {0.65f, 0.70f, 0.0f},
    {0.65f, 0.58f, 0.0f}, {0.65f, 0.50f, 0.0f}, {0.65f, 0.44f, 0.0f},
  };

  int i;
  UNUSED(image);
  UNUSED(width);
  UNUSED(height);

  for (i = 0; i < SIGNBRIDGE_NUM_LANDMARKS; i++)
    {
      landmarks[i].x = stub[i][0];
      landmarks[i].y = stub[i][1];
      landmarks[i].z = stub[i][2];
    }

  return OK;
}

int signbridge_run_classify(const struct signbridge_landmark_s *window,
                            int frames,
                            struct signbridge_result_s *result)
{
#ifdef CONFIG_TFLITEMICRO
  /* TODO: Flatten landmark window → quantize → invoke classifier
   * → softmax → argmax → fill result
   */
#endif

  UNUSED(window);
  UNUSED(frames);

  result->class_id    = 0;
  result->confidence  = 95;
  result->timestamp_ms = 0;

  return OK;
}

void signbridge_infer_deinit(void)
{
#ifdef CONFIG_TFLITEMICRO
  if (g_hand_lm_arena != NULL)
    {
      kmm_free(g_hand_lm_arena);
      g_hand_lm_arena = NULL;
    }

  if (g_hand_lm_model_buf != NULL)
    {
      kmm_free(g_hand_lm_model_buf);
      g_hand_lm_model_buf = NULL;
    }

  g_hand_lm_interp  = NULL;
  g_hand_lm_model   = NULL;
#endif
}

} /* extern "C" */
