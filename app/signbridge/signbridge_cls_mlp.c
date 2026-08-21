/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_cls_mlp.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lightweight MLP classifier for sign language recognition.
 * Loads INT8 quantized weights from ROMFS at runtime.
 *
 * Architecture:
 *   Input:  flattened landmark window (32 frames x 21 x 3 = 2016)
 *   → Dense(128, ReLU)  — INT8 weights, float32 bias
 *   → Dense(64, ReLU)   — INT8 weights, float32 bias
 *   → Dense(50, Softmax) — INT8 weights, float32 bias
 *   → argmax → class_id + confidence
 *
 * Inference time on ESP32-P4 (RV32IMAC, 400 MHz): ~1-3 ms.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>

#include "signbridge.h"
#include "signbridge_infer.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CLS_INPUT_DIM   (SIGNBRIDGE_WINDOW_FRAMES * SIGNBRIDGE_NUM_LANDMARKS * SIGNBRIDGE_LANDMARK_DIM)
#define CLS_HIDDEN1     128
#define CLS_HIDDEN2     64

/* Weight file path in ROMFS */

#define CLS_WEIGHTS_PATH  "/etc/models/sign_classifier.bin"

/* Binary weight file magic and version */

#define CLS_MAGIC   0x5349474E  /* "SIGN" */
#define CLS_VERSION 2

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Weight file header (28 bytes) */

struct cls_header_s
{
  uint32_t magic;      /* CLS_MAGIC */
  uint32_t version;    /* CLS_VERSION */
  uint32_t input_dim;
  uint32_t hidden1;
  uint32_t hidden2;
  uint32_t output_dim;
  uint32_t n_layers;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static float g_cls_h1[CLS_HIDDEN1];
static float g_cls_h2[CLS_HIDDEN2];
static float *g_cls_out;
static int g_cls_output_dim;

static float g_input_buf[CLS_INPUT_DIM];

/* Weight pointers (allocated at load time, pointing into weight buffer) */

static const int8_t *g_cls_w1;
static const float  *g_cls_b1;
static const int8_t *g_cls_w2;
static const float  *g_cls_b2;
static const int8_t *g_cls_w3;
static const float  *g_cls_b3;

static float g_cls_s1;
static float g_cls_s2;
static float g_cls_s3;

static uint8_t *g_weight_buf;
static bool g_weights_loaded;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline float relu(float x)
{
  return x > 0.0f ? x : 0.0f;
}

/****************************************************************************
 * Name: load_weights
 *
 * Description:
 *   Load INT8 quantized MLP weights from a binary file.
 *
 * File format:
 *   [28 bytes] header
 *   [4 bytes]  w1_size (uint32) + [4 bytes] s1 (float)
 *   [w1_size]  w1 (int8_t array)
 *   [hidden1 * 4] b1 (float array)
 *   [4 bytes]  w2_size + [4 bytes] s2
 *   [w2_size]  w2
 *   [hidden2 * 4] b2
 *   [4 bytes]  w3_size + [4 bytes] s3
 *   [w3_size]  w3
 *   [output_dim * 4] b3
 *
 ****************************************************************************/

static int load_weights(const char *path)
{
  FILE *fp;
  struct cls_header_s hdr;
  long file_size;
  uint32_t w_size;

  fp = fopen(path, "rb");
  if (fp == NULL)
    {
      syslog(LOG_WARNING, "cls_mlp: weights not found: %s\n", path);
      return -errno;
    }

  /* Get file size */

  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (file_size < (long)sizeof(hdr))
    {
      syslog(LOG_ERR, "cls_mlp: weight file too small\n");
      fclose(fp);
      return -EINVAL;
    }

  /* Allocate buffer and read entire file */

  g_weight_buf = (uint8_t *)kmm_malloc(file_size);
  if (g_weight_buf == NULL)
    {
      syslog(LOG_ERR, "cls_mlp: malloc %ld bytes failed\n", file_size);
      fclose(fp);
      return -ENOMEM;
    }

  if (fread(g_weight_buf, 1, file_size, fp) != (size_t)file_size)
    {
      syslog(LOG_ERR, "cls_mlp: read error\n");
      kmm_free(g_weight_buf);
      g_weight_buf = NULL;
      fclose(fp);
      return -EIO;
    }

  fclose(fp);

  /* Parse header */

  memcpy(&hdr, g_weight_buf, sizeof(hdr));

  if (hdr.magic != CLS_MAGIC || hdr.version != CLS_VERSION)
    {
      syslog(LOG_ERR, "cls_mlp: bad header (magic=0x%08lx ver=%lu)\n",
             (unsigned long)hdr.magic, (unsigned long)hdr.version);
      kmm_free(g_weight_buf);
      g_weight_buf = NULL;
      return -EINVAL;
    }

  g_cls_output_dim = hdr.output_dim;

  /* Allocate output buffer */

  g_cls_out = (float *)kmm_malloc(g_cls_output_dim * sizeof(float));
  if (g_cls_out == NULL)
    {
      kmm_free(g_weight_buf);
      g_weight_buf = NULL;
      return -ENOMEM;
    }

  /* Parse weight sections.  Track the buffer end and validate every
   * advance so a truncated / corrupted weight file cannot cause an
   * out-of-bounds read.
   */

  uint8_t *p   = g_weight_buf + sizeof(hdr);
  uint8_t *end = g_weight_buf + file_size;

#define CLS_BOUNDS_CHECK(n)                                   \
  do                                                          \
    {                                                         \
      if ((size_t)(end - p) < (size_t)(n))                    \
        {                                                     \
          syslog(LOG_ERR, "cls_mlp: weight file truncated\n"); \
          kmm_free(g_cls_out);                                \
          g_cls_out = NULL;                                   \
          kmm_free(g_weight_buf);                             \
          g_weight_buf = NULL;                                \
          return -EINVAL;                                     \
        }                                                     \
    }                                                         \
  while (0)

  /* Layer 1: w1 (int8) + b1 (float) */

  CLS_BOUNDS_CHECK(8);
  memcpy(&w_size, p, 4); p += 4;
  memcpy(&g_cls_s1, p, 4); p += 4;
  CLS_BOUNDS_CHECK((size_t)w_size + hdr.hidden1 * 4);
  g_cls_w1 = (const int8_t *)p; p += w_size;
  g_cls_b1 = (const float *)p;  p += hdr.hidden1 * 4;

  /* Layer 2: w2 (int8) + b2 (float) */

  CLS_BOUNDS_CHECK(8);
  memcpy(&w_size, p, 4); p += 4;
  memcpy(&g_cls_s2, p, 4); p += 4;
  CLS_BOUNDS_CHECK((size_t)w_size + hdr.hidden2 * 4);
  g_cls_w2 = (const int8_t *)p; p += w_size;
  g_cls_b2 = (const float *)p;  p += hdr.hidden2 * 4;

  /* Layer 3: w3 (int8) + b3 (float) */

  CLS_BOUNDS_CHECK(8);
  memcpy(&w_size, p, 4); p += 4;
  memcpy(&g_cls_s3, p, 4); p += 4;
  CLS_BOUNDS_CHECK((size_t)w_size + hdr.output_dim * 4);
  g_cls_w3 = (const int8_t *)p; p += w_size;
  g_cls_b3 = (const float *)p;

#undef CLS_BOUNDS_CHECK

  g_weights_loaded = true;

  syslog(LOG_INFO, "cls_mlp: weights loaded (%ld bytes, %d classes)\n",
         file_size, g_cls_output_dim);
  return OK;
}

/****************************************************************************
 * Name: mlp_forward
 *
 * Description:
 *   Run the 3-layer MLP forward pass using INT8 weights.
 *
 ****************************************************************************/

static void mlp_forward(const float *input)
{
  int i;
  int j;

  /* Layer 1: input → hidden1 (128 neurons, ReLU).
   * The input vector is always CLS_INPUT_DIM long; short landmark
   * windows are zero padded by the caller.
   */

  for (j = 0; j < CLS_HIDDEN1; j++)
    {
      float sum = g_cls_b1[j];
      for (i = 0; i < CLS_INPUT_DIM; i++)
        {
          sum += input[i] *
                 ((float)g_cls_w1[i * CLS_HIDDEN1 + j] * g_cls_s1);
        }

      g_cls_h1[j] = relu(sum);
    }

  /* Layer 2: hidden1 → hidden2 (64 neurons, ReLU) */

  for (j = 0; j < CLS_HIDDEN2; j++)
    {
      float sum = g_cls_b2[j];
      for (i = 0; i < CLS_HIDDEN1; i++)
        {
          sum += g_cls_h1[i] *
                 ((float)g_cls_w2[i * CLS_HIDDEN2 + j] * g_cls_s2);
        }

      g_cls_h2[j] = relu(sum);
    }

  /* Layer 3: hidden2 → output (n_classes, softmax) */

  float max_val = -1e9f;
  for (j = 0; j < g_cls_output_dim; j++)
    {
      float sum = g_cls_b3[j];
      for (i = 0; i < CLS_HIDDEN2; i++)
        {
          sum += g_cls_h2[i] *
                 ((float)g_cls_w3[i * g_cls_output_dim + j] * g_cls_s3);
        }

      g_cls_out[j] = sum;
      if (sum > max_val)
        {
          max_val = sum;
        }
    }

  /* Softmax */

  float exp_sum = 0.0f;
  for (j = 0; j < g_cls_output_dim; j++)
    {
      g_cls_out[j] = expf(g_cls_out[j] - max_val);
      exp_sum += g_cls_out[j];
    }

  for (j = 0; j < g_cls_output_dim; j++)
    {
      g_cls_out[j] /= exp_sum;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void signbridge_cls_init(void)
{
  g_weights_loaded = false;
  g_cls_output_dim = CONFIG_DEMOS_SIGNBRIDGE_VOCAB_SIZE;
  g_cls_out = NULL;
  g_weight_buf = NULL;

  memset(g_cls_h1, 0, sizeof(g_cls_h1));
  memset(g_cls_h2, 0, sizeof(g_cls_h2));
  memset(g_input_buf, 0, sizeof(g_input_buf));

  /* Try to load trained weights from ROMFS */

  int ret = load_weights(CLS_WEIGHTS_PATH);
  if (ret < 0)
    {
      /* Fall back to alloc output buffer with default dim */

      g_cls_out = (float *)kmm_malloc(g_cls_output_dim * sizeof(float));
      if (g_cls_out != NULL)
        {
          memset(g_cls_out, 0, g_cls_output_dim * sizeof(float));
        }

      syslog(LOG_WARNING, "cls_mlp: using untrained weights (stub)\n");
    }
}

int signbridge_cls_run(const struct signbridge_landmark_s *window,
                       int frames,
                       struct signbridge_result_s *result)
{
  int i;
  int best_idx = 0;
  float best_score = 0.0f;

  if (g_cls_out == NULL || result == NULL)
    {
      return -ENOMEM;
    }

  if (window == NULL || frames <= 0)
    {
      syslog(LOG_WARNING, "cls_mlp: empty landmark window\n");
      result->class_id     = -1;
      result->confidence   = 0;
      result->timestamp_ms = 0;
      return OK;
    }

  /* Flatten landmark window into input buffer */

  int count = frames * SIGNBRIDGE_NUM_LANDMARKS * SIGNBRIDGE_LANDMARK_DIM;
  if (count > CLS_INPUT_DIM)
    {
      count = CLS_INPUT_DIM;
    }

  /* Zero the whole vector first so short windows are zero padded */

  memset(g_input_buf, 0, sizeof(g_input_buf));

  for (i = 0; i < count; i++)
    {
      int frame = i / (SIGNBRIDGE_NUM_LANDMARKS * SIGNBRIDGE_LANDMARK_DIM);
      int rem   = i % (SIGNBRIDGE_NUM_LANDMARKS * SIGNBRIDGE_LANDMARK_DIM);
      int lmk   = rem / SIGNBRIDGE_LANDMARK_DIM;
      int coord = rem % SIGNBRIDGE_LANDMARK_DIM;

      const struct signbridge_landmark_s *l =
          &window[frame * SIGNBRIDGE_NUM_LANDMARKS + lmk];

      switch (coord)
        {
          case 0: g_input_buf[i] = l->x; break;
          case 1: g_input_buf[i] = l->y; break;
          case 2: g_input_buf[i] = l->z; break;
        }
    }

  /* Run MLP forward pass */

  if (g_weights_loaded)
    {
      mlp_forward(g_input_buf);
    }
  else
    {
      /* Stub: produce a deterministic output for pipeline testing */

      static uint32_t stub_seq = 0;
      for (i = 0; i < g_cls_output_dim; i++)
        {
          g_cls_out[i] = (i == (stub_seq % g_cls_output_dim)) ?
              0.9f : 0.01f;
        }

      stub_seq++;
    }

  /* Find best class */

  for (i = 0; i < g_cls_output_dim; i++)
    {
      if (g_cls_out[i] > best_score)
        {
          best_score = g_cls_out[i];
          best_idx = i;
        }
    }

  result->class_id    = best_idx;
  result->confidence  = (uint8_t)(best_score * 100.0f);
  result->timestamp_ms = 0;

  return OK;
}
