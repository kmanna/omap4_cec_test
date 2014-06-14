LOCAL_PATH := $(call my-dir)

cec_test_includes := \


#
# cec_test
#
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := $(cec_test_includes)
LOCAL_MODULE := cec_test
LOCAL_MODULE_TAGS=optional
LOCAL_SHARED_LIBRARIES := liblog libcutils libbinder libutils
LOCAL_SRC_FILES := cec_test.c decoder.c
include $(BUILD_EXECUTABLE)

#
# cec_test (host)
#
#include $(CLEAR_VARS)
#LOCAL_C_INCLUDES := $(cec_test_includes)
#LOCAL_LDLIBS := -lrt
#LOCAL_MODULE := cec_test
#LOCAL_MODULE_TAGS=optional
#LOCAL_SRC_FILES := cec_test.c
#include $(BUILD_HOST_EXECUTABLE)
