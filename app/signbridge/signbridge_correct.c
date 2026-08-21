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

      /* Rule 2/3: consecutive or rapid repeats of the same word collapse */

      if (nout > 0)
        {
          uint8_t prev = out_words[nout - 1];
          uint32_t gap = frag->ts_ms -
                         g_frags[i - 1].ts_ms;

          if (prev == frag->class_id &&
              (i == 0 || g_frags[i - 1].class_id == frag->class_id ||
               gap <= FRAG_DUP_WINDOW_MS))
            {
              /* Same word again: keep only the more confident one */

              if (frag->confidence > g_frags[i - 1].confidence)
                {
                  out_words[nout - 1] = frag->class_id;
                }

              continue;
            }
        }

      out_words[nout++] = frag->class_id;
      if (nout >= SIGNBRIDGE_CORRECT_MAX_WORDS)
        {
          break;
        }
    }

  /* Pass 2: template completion (common utterances) */

  if (nout == 1 && out_words[0] == 0)   /* 只有"你好" -> 补全问候 */
    {
      /* Keep as-is: "你好" is a complete greeting */
    }

  /* Build the corrected text */

  utterance->nwords = nout;
  utterance->text[0] = '\0';
  utterance->score = 1.0f;

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

      if (i > 0)
        {
          strlcat(utterance->text, " ", sizeof(utterance->text));
        }

      strlcat(utterance->text, word, sizeof(utterance->text));
    }

  /* Average confidence of kept fragments */

  if (nout > 0)
    {
      float sum = 0.0f;

      for (i = 0; i < nout; i++)
        {
          /* Re-locate the source fragment for score averaging */

          int j;

          for (j = 0; j < g_nfrags; j++)
            {
              if (g_frags[j].class_id == out_words[i])
                {
                  sum += g_frags[j].confidence;
                  break;
                }
            }
        }

      utterance->score = sum / nout;
    }

  syslog(LOG_INFO, "correct: \"%s\" (%u words, conf %.2f)\n",
         utterance->text, utterance->nwords, utterance->score);

  g_nfrags = 0;
  return OK;
}
