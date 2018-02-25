LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := game
LOCAL_MODULE_FILENAME := lib$(LOCAL_MODULE)_android_$(TARGET_ARCH_ABI)

LOCAL_CFLAGS := -DGAME_MODULE
LOCAL_C_INCLUDES := \
    $(QFUSION_PATH)/third-party/angelscript/sdk/angelscript/include \
    $(QFUSION_PATH)/third-party/recastnavigation/Recast/Include \
    $(QFUSION_PATH)/third-party/recastnavigation/Detour/Include

LOCAL_SRC_FILES := \
  ../gameshared/gs_gameteams.c \
  ../gameshared/gs_items.c \
  ../gameshared/gs_misc.c \
  ../gameshared/gs_players.c \
  ../gameshared/gs_pmove.c \
  ../gameshared/gs_slidebox.c \
  ../gameshared/gs_weapondefs.c \
  ../gameshared/gs_weapons.c \
  ../gameshared/q_math.c \
  ../gameshared/q_shared.c \
  ../matchmaker/mm_rating.c \
  ../qalgo/base64.c \
  ../qalgo/md5.c \
  $(addprefix ai/,$(notdir $(wildcard $(LOCAL_PATH)/ai/*.cpp))) \
  $(notdir $(wildcard $(LOCAL_PATH)/*.cpp)) \
  $(wildcard $(QFUSION_PATH)/third-party/recastnavigation/Recast/Source/*.cpp) \
  $(wildcard $(QFUSION_PATH)/third-party/recastnavigation/Detour/Source/*.cpp)

include $(BUILD_SHARED_LIBRARY)
