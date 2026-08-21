/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_correct.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Semantic correction module for SignBridge.
 *
 * Plan: 云端小模型对端侧识别碎片（词 + 置信度 + 时间戳）做上下文
 * 拼接与纠错。离线降级版：纯本地规则拼接 —— 去重、高频词优先、
 * 常用语模板补全。云端路径留接口，WiFi6（ESP32-C6 SDIO）就绪后
 * 接入。
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_CORRECT_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_CORRECT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>

#include "signbridge.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SIGNBRIDGE_CORRECT_MAX_WORDS   16   /* 一句话最多拼接的词数 */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* 识别碎片：一个词 + 置信度 + 时间戳 */

struct signbridge_fragment_s
{
  uint8_t  class_id;        /* 词表索引                  */
  float    confidence;      /* 0.0 ~ 1.0                 */
  uint32_t ts_ms;           /* 识别时刻（单调时钟）       */
};

/* 拼接结果 */

struct signbridge_utterance_s
{
  char     text[SIGNBRIDGE_CORRECT_MAX_WORDS * 8 + 1];  /* 纠错后文本 */
  uint8_t  words[SIGNBRIDGE_CORRECT_MAX_WORDS];         /* 词 id 序列 */
  uint8_t  nwords;                                      /* 词数       */
  float    score;                                       /* 整体置信度 */
};

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_correct_init
 ****************************************************************************/

int signbridge_correct_init(void);

/****************************************************************************
 * Name: signbridge_correct_reset
 *
 * Description:
 *   Clear the current utterance buffer (new sentence starts).
 *
 ****************************************************************************/

void signbridge_correct_reset(void);

/****************************************************************************
 * Name: signbridge_correct_add
 *
 * Description:
 *   Add one recognition fragment to the utterance.
 *
 ****************************************************************************/

int signbridge_correct_add(const struct signbridge_fragment_s *frag);

/****************************************************************************
 * Name: signbridge_correct_flush
 *
 * Description:
 *   Finalize the utterance: apply local rules (dedup, freq-priority,
 *   template completion) and produce the corrected text.
 *
 * Returned Value:
 *   OK on success; utterance is filled.
 *
 ****************************************************************************/

int signbridge_correct_flush(struct signbridge_utterance_s *utterance);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_CORRECT_H */
