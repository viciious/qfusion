LOCAL_PATH := $(call my-dir)/..
include $(CLEAR_VARS)
LOCAL_MODULE := qfusion

LOCAL_CFLAGS := -DUSE_OPENSSL
ifdef QFUSION_APPLICATION_VERSION_HEADER
  LOCAL_CFLAGS += -DAPPLICATION_VERSION_HEADER=\"$(QFUSION_APPLICATION_VERSION_HEADER)\"
endif

LOCAL_LDLIBS := -landroid -llog -lz
LOCAL_STATIC_LIBRARIES := curl
LOCAL_WHOLE_STATIC_LIBRARIES := android_native_app_glue
LOCAL_DISABLE_FATAL_LINKER_WARNINGS := true

LOCAL_SRC_FILES := \
  android/android_client.c \
  android/android_clipboard.c \
  android/android_console.c \
  android/android_input.c \
  android/android_lib.c \
  android/android_sys.c \
  android/android_time.c \
  android/android_vfs.c \
  android/android_vid.c \
  $(addprefix client/,$(notdir $(wildcard $(LOCAL_PATH)/client/*.c))) \
  gameshared/q_math.c \
  gameshared/q_shared.c \
  $(addprefix matchmaker/,$(notdir $(wildcard $(LOCAL_PATH)/matchmaker/*.c))) \
  qalgo/base64.c \
  qalgo/glob.c \
  qalgo/hash.c \
  qalgo/half_float.c \
  qalgo/md5.c \
  qalgo/q_trie.c \
  $(addprefix qcommon/,$(notdir $(wildcard $(LOCAL_PATH)/qcommon/*.c))) \
  $(addprefix qcommon/,$(notdir $(wildcard $(LOCAL_PATH)/qcommon/*.cpp))) \
  $(addprefix server/,$(notdir $(wildcard $(LOCAL_PATH)/server/*.c))) \
  unix/unix_fs.c \
  unix/unix_net.c \
  unix/unix_threads.c

include $(BUILD_SHARED_LIBRARY)
