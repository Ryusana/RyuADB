TARGET := iphone:clang:latest:15.0
ARCHS = arm64
THEOS_PACKAGE_SCHEME = rootless
include $(THEOS)/makefiles/common.mk
TOOL_NAME = ryucalctap
ryucalctap_FILES = main.c
ryucalctap_CFLAGS = -std=c11 -Wall -Wextra -O2
ryucalctap_FRAMEWORKS = CoreFoundation
include $(THEOS_MAKE_PATH)/tool.mk
