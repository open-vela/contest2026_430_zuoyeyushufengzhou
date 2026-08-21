/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_vocab.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared sign-language vocabulary table.
 *
 * The 50-word vocabulary matches:
 *   - tools/train_sign_classifier_tf.py DEFAULT_VOCAB (training labels)
 *   - media/signs/<id>.wav (voice library, class_id -> WAV file)
 *   - signbridge_correct.c (utterance assembly)
 *   - signbridge_ui.c (result display)
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_VOCAB_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_VOCAB_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SIGNBRIDGE_VOCAB_SIZE   50

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: signbridge_vocab_get
 *
 * Description:
 *   Return the word string for a class id, or NULL if out of range.
 *
 ****************************************************************************/

const char *signbridge_vocab_get(int class_id);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_VOCAB_H */
