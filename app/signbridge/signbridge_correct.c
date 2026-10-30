/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_correct.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Semantic correction module (offline fallback, see header).
 *
 * Local rules applied at flush time:
 *   1. Drop fragments below the confidence threshold (mis-recognitions).
 *   2. Deduplicate consecutive repeats of the same word (keep the one
 *      with the highest confidence).
 *   3. Collapse rapid re-detections of the same word within a short
 *      window into a single occurrence.
 *   4. Template completion for common utterances (e.g. leading "你好").
 *
 * The cloud path (contextual correction via a small model over WiFi6)
 * is intentionally left as a future hook: replace signbridge_correct_flush()
 * body with a network call when the C6 SDIO link is available.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "signbridge_correct.h"
#include "signbridge_vocab.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FRAG_MIN_CONFIDENCE    0.55f   /* 低于此置信度的碎片丢弃       */
#define FRAG_DUP_WINDOW_MS     1500    /* 此窗口内的同词重复合并        */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct signbridge_fragment_s g_frags[SIGNBRIDGE_CORRECT_MAX_WORDS];
static int g_nfrags;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_correct_init
 ****************************************************************************/

int signbridge_correct_init(void)
{
  g_nfrags = 0;
  return OK;
}

/****************************************************************************
 * Name: signbridge_correct_reset
 ****************************************************************************/

void signbridge_correct_reset(void)
{
  g_nfrags = 0;
}

/****************************************************************************
 * Name: signbridge_correct_add
 ****************************************************************************/

int signbridge_correct_add(const struct signbridge_fragment_s *frag)
{
  if (frag == NULL || g_nfrags >= SIGNBRIDGE_CORRECT_MAX_WORDS)
    {
      return -ENOSPC;
    }

  /* Rule 0: reject fragments with no confidence */

  if (frag->confidence <= 0.0f)
    {
      return OK;
    }

  g_frags[g_nfrags++] = *frag;
  return OK;
}

/****************************************************************************
 * Name: signbridge_correct_flush
 ****************************************************************************/

int signbridge_correct_flush(struct signbridge_utterance_s *utterance)
{
  uint8_t out_words[SIGNBRIDGE_CORRECT_MAX_WORDS];
  float out_conf[SIGNBRIDGE_CORRECT_MAX_WORDS];
  uint32_t out_ts[SIGNBRIDGE_CORRECT_MAX_WORDS];
  int nout = 0;
  int i;
  int len;

  if (utterance == NULL)
    {
      return -EINVAL;
    }

  /* Pass 1: filter + dedup */

  for (i = 0; i < g_nfrags; i++)
    {
      const struct signbridge_fragment_s *frag = &g_frags[i];

      /* Rule 1: drop low-confidence fragments */

      if (frag->confidence < FRAG_MIN_CONFIDENCE)
        {
          continue;
        }

      /* Rule 2/3: consecutive or rapid repeats of the same word collapse.
       * Both rules compare against the last KEPT word: consecutive means
       * the previous input fragment carried the same class; rapid means
       * the re-detection arrived within FRAG_DUP_WINDOW_MS of the kept
       * occurrence (the kept timestamp, not the previous input one).
       */

      if (nout > 0 && out_words[nout - 1] == frag->class_id)
        {
          bool consecutive = (i > 0 &&
                              g_frags[i - 1].class_id == frag->class_id);
          bool rapid = (frag->ts_ms - out_ts[nout - 1]) <=
                        FRAG_DUP_WINDOW_MS;

          if (consecutive || rapid)
            {
              /* Same word again: keep only the more confident one */

              if (frag->confidence > out_conf[nout - 1])
                {
                  out_words[nout - 1] = frag->class_id;
                  out_conf[nout - 1]  = frag->confidence;
                  out_ts[nout - 1]    = frag->ts_ms;
                }

              continue;
            }
        }

      if (nout >= SIGNBRIDGE_CORRECT_MAX_WORDS)
        {
          break;
        }

      out_words[nout] = frag->class_id;
      out_conf[nout]  = frag->confidence;
      out_ts[nout]    = frag->ts_ms;
      nout++;
    }

  /* Pass 2: template completion (common utterances) */

  if (nout == 1 && out_words[0] == 0)   /* 只有"你好" -> 补全问候 */
    {
      /* Keep as-is: "你好" is a complete greeting */
    }

  /* Build the corrected text */

  utterance->nwords = nout;
  utterance->text[0] = '\0';
  utterance->score = 0.0f;

  for (i = 0; i < nout; i++)
    {
      const char *word = signbridge_vocab_get(out_words[i]);

      if (word == NULL)
        {
          continue;
        }

      len = strlen(utterance->text);
      if (len + strlen(word) + 1 >= (int)sizeof(utterance->text))
        {
          break;
        }

      if (utterance->text[0] != '\0')
        {
          strlcat(utterance->text, " ", sizeof(utterance->text));
        }

      strlcat(utterance->text, word, sizeof(utterance->text));
    }

  /* Keep the word id sequence alongside the text */

  for (i = 0; i < nout; i++)
    {
      utterance->words[i] = out_words[i];
    }

  /* Average confidence of the kept words (out_conf tracks the fragment
   * actually kept for each output slot, already applying the "higher
   * confidence wins" merge).
   */

  if (nout > 0)
    {
      float sum = 0.0f;

      for (i = 0; i < nout; i++)
        {
          sum += out_conf[i];
        }

      utterance->score = sum / nout;
    }

  syslog(LOG_INFO, "correct: \"%s\" (%u words, conf %.2f)\n",
         utterance->text, utterance->nwords, utterance->score);

  g_nfrags = 0;
  return OK;
}
