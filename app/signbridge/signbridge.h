/****************************************************************************
 * app/signbridge/signbridge.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Hand landmark model outputs 21 keypoints x 3 coordinates */

#define SIGNBRIDGE_NUM_LANDMARKS   21
#define SIGNBRIDGE_LANDMARK_DIM    3

/* Sliding window of landmark frames fed to the temporal classifier */

#define SIGNBRIDGE_WINDOW_FRAMES   32

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Application states: idle -> detecting -> recognizing -> result */

enum signbridge_state_e
{
  SIGNBRIDGE_STATE_IDLE = 0,      /* Low-power standby, camera stopped  */
  SIGNBRIDGE_STATE_DETECTING,     /* Camera streaming, waiting for hand */
  SIGNBRIDGE_STATE_RECOGNIZING,   /* Hand tracked, running classifier   */
  SIGNBRIDGE_STATE_RESULT,        /* Showing recognition result         */
};

/* One recognition event delivered to the UI layer */

struct signbridge_result_s
{
  int      class_id;              /* Vocabulary index, -1 if invalid    */
  uint8_t  confidence;            /* 0..100 percent                     */
  uint32_t timestamp_ms;
};

/* Hand landmark result (21 keypoints x 3 coordinates) */

struct signbridge_landmark_s
{
  float x;
  float y;
  float z;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int signbridge_sm_init(void);
void signbridge_sm_step(void);
enum signbridge_state_e signbridge_sm_state(void);
const struct signbridge_result_s *signbridge_sm_last_result(void);
int signbridge_sm_post_result(const struct signbridge_result_s *result);
const char *signbridge_sm_utterance_text(void);

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_H */
