############################################################################
# app/signbridge/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# SignBridge: on-device sign language recognition terminal

PROGNAME  = $(CONFIG_DEMOS_SIGNBRIDGE_PROGNAME)
PRIORITY  = $(CONFIG_DEMOS_SIGNBRIDGE_PRIORITY)
STACKSIZE = $(CONFIG_DEMOS_SIGNBRIDGE_STACKSIZE)
MODULE    = $(CONFIG_DEMOS_SIGNBRIDGE)

CSRCS   = signbridge_sm.c signbridge_infer.c signbridge_cls_mlp.c
CSRCS  += signbridge_voice.c
CSRCS  += signbridge_camera.c signbridge_anim.c signbridge_ui.c
CSRCS  += signbridge_vocab.c signbridge_correct.c signbridge_pm.c
CSRCS  += signbridge_audio_in.c
MAINSRC = signbridge_main.c

# TFLite Micro inference backend (C++ when TFLITEMICRO is enabled)
# Provides hand landmark detection using MediaPipe TFLite model

ifeq ($(CONFIG_TFLITEMICRO),y)
CXXSRCS = signbridge_infer_tflm.cc
CXXEXT  = .cc
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/mlearning/tflite-micro/tflite-micro
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy

# Match the tflite-micro library build: strip error strings so
# MicroPrintf() compiles to a no-op instead of an unresolved symbol.
CXXFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif

include $(APPDIR)/Application.mk
