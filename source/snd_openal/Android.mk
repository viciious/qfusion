LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := snd_openal
LOCAL_MODULE_FILENAME := lib$(LOCAL_MODULE)_android_$(TARGET_ARCH_ABI)

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../snd_common $(LOCAL_PATH)

LOCAL_STATIC_LIBRARIES := OpenAL-MOB vorbis

LOCAL_SRC_FILES := \
  ../gameshared/q_math.c \
  ../gameshared/q_shared.c \
  $(addprefix ../snd_common/,$(notdir $(wildcard $(LOCAL_PATH)/../snd_common/*.c))) \
  $(notdir $(wildcard $(LOCAL_PATH)/*.c)) \
  $(notdir $(wildcard $(LOCAL_PATH)/*.cpp))

include $(BUILD_SHARED_LIBRARY)
