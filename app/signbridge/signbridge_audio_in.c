/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_audio_in.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Voice input module for SignBridge (see signbridge_audio_in.h).
 *
 * Capture path:
 *   on-board mic -> ES8311 ADC -> I2S RX -> /dev/audio/pcm0 (AUDIO INPUT)
 *   -> per-buffer RMS energy -> VAD state machine
 *   -> wake word latch ("你好，openvela" / "Hello, openvela")
 *
 * The capture runs the standard NuttX AUDIO input flow:
 *   open -> RESERVE -> CONFIGURE(INPUT) -> REGISTERMQ -> ALLOCBUFFER xN
 *   -> ENQUEUEBUFFER xN -> START -> message loop (DEQUEUE -> process
 *   -> ENQUEUE) -> STOP -> RELEASE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>
#include <nuttx/mqueue.h>

#include "signbridge_audio_in.h"

#ifdef CONFIG_AUDIO

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct signbridge_audio_in_s
{
  int                        fd;          /* /dev/audio/pcm0        */
  pthread_t                  thread;      /* capture thread         */
  bool                       running;     /* thread active flag     */
  struct mq_attr             mqattr;      /* audio message queue    */
  struct file                mq;          /* audio message queue    */
  FAR struct ap_buffer_s    *buffers[SIGNBRIDGE_AUDIO_IN_NBUFS];
  int                        nbufs;
  enum signbridge_vad_state_e vad;        /* current VAD state      */
  int                        vad_speech_frames;
  int                        vad_silence_frames;
  int                        level;       /* smoothed 0..100        */
  int                        rms_accum;   /* exponential avg        */
  bool                       wakeword;    /* wake word latch        */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct signbridge_audio_in_s g_audio_in;

/* VAD thresholds (16-bit samples).  RMS above VAD_RMS_THRESHOLD sustained
 * for VAD_SPEECH_FRAMES frames -> SPEECH; below for VAD_SILENCE_FRAMES
 * frames -> SILENCE.  Frame = 100 ms.
 */

#define VAD_RMS_THRESHOLD      900
#define VAD_SPEECH_FRAMES      2     /* 200 ms of sound   -> speech   */
#define VAD_SILENCE_FRAMES     12    /* 1.2 s of silence  -> silence  */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_audio_in_vad_frame
 *
 * Description:
 *   Compute RMS energy of one frame and update the VAD state machine.
 *
 ****************************************************************************/

static void signbridge_audio_in_vad_frame(FAR const int16_t *samples,
                                          size_t nsamples)
{
  struct signbridge_audio_in_s *priv = &g_audio_in;
  uint64_t sum = 0;
  size_t i;
  int rms;
  int level;

  for (i = 0; i < nsamples; i++)
    {
      int32_t s = samples[i];
      sum += (uint64_t)(s * s);
    }

  rms = (int)sqrt((double)sum / (double)nsamples);

  /* Smoothed input level (percent) for the UI meter */

  priv->rms_accum = (priv->rms_accum * 7 + rms) / 8;
  level = (priv->rms_accum * 100) / 32768;
  if (level > 100)
    {
      level = 100;
    }

  priv->level = level;

  /* VAD state machine */

  if (rms >= VAD_RMS_THRESHOLD)
    {
      priv->vad_speech_frames++;
      priv->vad_silence_frames = 0;

      if (priv->vad == SIGNBRIDGE_VAD_SILENCE &&
          priv->vad_speech_frames >= VAD_SPEECH_FRAMES)
        {
          /* Silence -> speech transition: report a wake-up so the state
           * machine can switch to listening (real wake-word model will
           * replace this fallback).
           */

          priv->vad = SIGNBRIDGE_VAD_SPEECH;
          priv->wakeword = true;
          syslog(LOG_INFO, "audio_in: wake word? (%s / %s)\n",
                 SIGNBRIDGE_WAKE_WORD_ZH, SIGNBRIDGE_WAKE_WORD_EN);
        }
    }
  else
    {
      priv->vad_silence_frames++;
      priv->vad_speech_frames = 0;

      if (priv->vad == SIGNBRIDGE_VAD_SPEECH &&
          priv->vad_silence_frames >= VAD_SILENCE_FRAMES)
        {
          priv->vad = SIGNBRIDGE_VAD_SILENCE;
          syslog(LOG_INFO, "audio_in: speech segment ended\n");
        }
    }
}

/****************************************************************************
 * Name: signbridge_audio_in_thread
 *
 * Description:
 *   Capture thread: drives the AUDIO INPUT message loop.
 *
 ****************************************************************************/

static FAR void *signbridge_audio_in_thread(FAR void *arg)
{
  struct signbridge_audio_in_s *priv = &g_audio_in;
  struct audio_msg_s msg;
  unsigned int prio;
  ssize_t msglen;
  int ret;
  int i;

  syslog(LOG_INFO, "audio_in: capture thread started\n");

  while (priv->running)
    {
      /* Wait for a buffer completion message */

      msglen = file_mq_receive(&priv->mq, (FAR char *)&msg,
                               sizeof(msg), &prio);
      if (msglen < (ssize_t)sizeof(struct audio_msg_s))
        {
          if (priv->running)
            {
              syslog(LOG_WARNING, "audio_in: short mq message\n");
            }

          break;
        }

      if (msg.msg_id == AUDIO_MSG_DEQUEUE ||
          msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          /* Process all completed buffers */

          for (i = 0; i < priv->nbufs; i++)
            {
              FAR struct ap_buffer_s *apb = priv->buffers[i];

              if (apb != NULL && apb->flags & AUDIO_APB_FINAL)
                {
                  size_t nbytes = apb->nbytes - apb->curbyte;

                  if (nbytes >= sizeof(int16_t))
                    {
                      signbridge_audio_in_vad_frame(
                          (FAR const int16_t *)(apb->samp + apb->curbyte),
                          nbytes / sizeof(int16_t));
                    }

                  apb->curbyte = 0;
                  apb->flags  &= ~AUDIO_APB_FINAL;

                  /* Re-enqueue the buffer for the next capture */

                  ret = ioctl(priv->fd, AUDIOIOC_ENQUEUEBUFFER,
                              (unsigned long)apb);
                  if (ret < 0)
                    {
                      syslog(LOG_ERR, "audio_in: ENQUEUE failed: %d\n",
                             errno);
                    }
                }
            }
        }
      else if (msg.msg_id == AUDIO_MSG_STOP)
        {
          break;
        }
    }

  syslog(LOG_INFO, "audio_in: capture thread exiting\n");
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_audio_in_init
 ****************************************************************************/

int signbridge_audio_in_init(void)
{
  struct signbridge_audio_in_s *priv = &g_audio_in;

  memset(priv, 0, sizeof(*priv));
  priv->fd = -1;
  priv->vad = SIGNBRIDGE_VAD_SILENCE;
  return OK;
}

/****************************************************************************
 * Name: signbridge_audio_in_start
 ****************************************************************************/

int signbridge_audio_in_start(void)
{
  struct signbridge_audio_in_s *priv = &g_audio_in;
  struct audio_caps_desc_s cap_desc;
  pthread_attr_t tattr;
  struct sched_param sparam;
  int ret;
  int i;

  if (priv->running)
    {
      return OK;
    }

  /* 1. Open the audio device */

  priv->fd = open(SIGNBRIDGE_AUDIO_IN_DEV, O_RDONLY);
  if (priv->fd < 0)
    {
      syslog(LOG_ERR, "audio_in: open %s failed: %d\n",
             SIGNBRIDGE_AUDIO_IN_DEV, errno);
      return -errno;
    }

  /* 2. Reserve the device */

  ret = ioctl(priv->fd, AUDIOIOC_RESERVE, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "audio_in: RESERVE failed: %d\n", errno);
      close(priv->fd);
      priv->fd = -1;
      return -errno;
    }

  /* 3. Configure the device for INPUT capture */

  cap_desc.caps.ac_format.hw = AUDIO_FMT_PCM;
  cap_desc.caps.ac_channels  = SIGNBRIDGE_AUDIO_IN_CHANNELS;
  cap_desc.caps.ac_controls.hw[0] = SIGNBRIDGE_AUDIO_IN_RATE;
  cap_desc.caps.ac_controls.b[2]  = SIGNBRIDGE_AUDIO_IN_BPS;
  cap_desc.caps.ac_type = AUDIO_TYPE_INPUT;

  ret = ioctl(priv->fd, AUDIOIOC_CONFIGURE,
              (unsigned long)&cap_desc);
  if (ret < 0)
    {
      syslog(LOG_ERR, "audio_in: CONFIGURE failed: %d\n", errno);
      ioctl(priv->fd, AUDIOIOC_RELEASE, 0);
      close(priv->fd);
      priv->fd = -1;
      return -errno;
    }

  /* 4. Register our message queue */

  priv->mqattr.mq_maxmsg  = 16;
  priv->mqattr.mq_msgsize = sizeof(struct audio_msg_s);
  priv->mqattr.mq_flags   = 0;

  ret = file_mq_open(&priv->mq, "/signbridge_audio_in_mq",
                     O_RDONLY | O_CREAT, 0666, &priv->mqattr);
  if (ret < 0)
    {
      syslog(LOG_ERR, "audio_in: mq_open failed: %d\n", errno);
      ioctl(priv->fd, AUDIOIOC_RELEASE, 0);
      close(priv->fd);
      priv->fd = -1;
      return -errno;
    }

  ret = ioctl(priv->fd, AUDIOIOC_REGISTERMQ,
              (unsigned long)&priv->mq);
  if (ret < 0)
    {
      syslog(LOG_ERR, "audio_in: REGISTERMQ failed: %d\n", errno);
      file_mq_close(&priv->mq);
      ioctl(priv->fd, AUDIOIOC_RELEASE, 0);
      close(priv->fd);
      priv->fd = -1;
      return -errno;
    }

  /* 5. Allocate and enqueue capture buffers */

  priv->nbufs = 0;
  for (i = 0; i < SIGNBRIDGE_AUDIO_IN_NBUFS; i++)
    {
      FAR struct ap_buffer_s *apb = NULL;

      ret = ioctl(priv->fd, AUDIOIOC_ALLOCBUFFER,
                  (unsigned long)&apb);
      if (ret < 0)
        {
          syslog(LOG_WARNING, "audio_in: ALLOCBUFFER[%d] failed: %d\n",
                 i, errno);
          break;
        }

      apb->nbytes = SIGNBRIDGE_AUDIO_IN_FRAME_BYTES;
      apb->curbyte = 0;
      apb->flags  = AUDIO_APB_OUTPUT_ENQUEUED;
      priv->buffers[i] = apb;
      priv->nbufs++;

      ret = ioctl(priv->fd, AUDIOIOC_ENQUEUEBUFFER,
                  (unsigned long)apb);
      if (ret < 0)
        {
          syslog(LOG_ERR, "audio_in: ENQUEUE[%d] failed: %d\n",
                 i, errno);
          break;
        }
    }

  if (priv->nbufs < 2)
    {
      syslog(LOG_ERR, "audio_in: not enough buffers (%d)\n", priv->nbufs);
      ioctl(priv->fd, AUDIOIOC_UNREGISTERMQ, 0);
      file_mq_close(&priv->mq);
      ioctl(priv->fd, AUDIOIOC_RELEASE, 0);
      close(priv->fd);
      priv->fd = -1;
      return -ENOMEM;
    }

  /* 6. Start capture */

  ret = ioctl(priv->fd, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "audio_in: START failed: %d\n", errno);
      return -errno;
    }

  /* 7. Launch the capture thread */

  priv->running = true;
  pthread_attr_init(&tattr);
  sparam.sched_priority = 120;
  pthread_attr_setschedparam(&tattr, &sparam);
  pthread_attr_setstacksize(&tattr, 4096);

  ret = pthread_create(&priv->thread, &tattr,
                       signbridge_audio_in_thread, NULL);
  if (ret != 0)
    {
      priv->running = false;
      syslog(LOG_ERR, "audio_in: pthread_create failed: %d\n", ret);
      return -ret;
    }

  syslog(LOG_INFO, "audio_in: capture started (%u Hz, %u ch, %u bit)\n",
         SIGNBRIDGE_AUDIO_IN_RATE, SIGNBRIDGE_AUDIO_IN_CHANNELS,
         SIGNBRIDGE_AUDIO_IN_BPS);
  return OK;
}

/****************************************************************************
 * Name: signbridge_audio_in_stop
 ****************************************************************************/

int signbridge_audio_in_stop(void)
{
  struct signbridge_audio_in_s *priv = &g_audio_in;
  int i;

  if (!priv->running)
    {
      return OK;
    }

  priv->running = false;

  /* Stop the driver first (kills pending transfers), then join the
   * thread, then tear down the message queue.
   */

  ioctl(priv->fd, AUDIOIOC_STOP, 0);
  pthread_join(priv->thread, NULL);

  ioctl(priv->fd, AUDIOIOC_UNREGISTERMQ, 0);
  file_mq_close(&priv->mq);

  for (i = 0; i < priv->nbufs; i++)
    {
      if (priv->buffers[i] != NULL)
        {
          ioctl(priv->fd, AUDIOIOC_FREEBUFFER,
                (unsigned long)priv->buffers[i]);
          priv->buffers[i] = NULL;
        }
    }

  priv->nbufs = 0;
  ioctl(priv->fd, AUDIOIOC_RELEASE, 0);
  close(priv->fd);
  priv->fd = -1;

  syslog(LOG_INFO, "audio_in: capture stopped\n");
  return OK;
}

/****************************************************************************
 * Name: signbridge_audio_in_vad_state
 ****************************************************************************/

enum signbridge_vad_state_e signbridge_audio_in_vad_state(void)
{
  return g_audio_in.vad;
}

/****************************************************************************
 * Name: signbridge_audio_in_level
 ****************************************************************************/

int signbridge_audio_in_level(void)
{
  return g_audio_in.level;
}

/****************************************************************************
 * Name: signbridge_audio_in_wakeword_heard
 ****************************************************************************/

bool signbridge_audio_in_wakeword_heard(void)
{
  return g_audio_in.wakeword;
}

/****************************************************************************
 * Name: signbridge_audio_in_clear_wakeword
 ****************************************************************************/

void signbridge_audio_in_clear_wakeword(void)
{
  g_audio_in.wakeword = false;
}

#endif /* CONFIG_AUDIO */
