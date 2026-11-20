TARGET := iphone:clang:latest:15.0
ARCHS = arm64
THEOS_PACKAGE_SCHEME = rootless
include $(THEOS)/makefiles/common.mk
TOOL_NAME = ryuvirtualhid
ryuvirtualhid_FILES = ryu_virtual_hid.c
ryuvirtualhid_CFLAGS = -std=c11 -Wall -Wextra -O2 -D_DARWIN_C_SOURCE
ryuvirtualhid_FRAMEWORKS = CoreFoundation
ryuvirtualhid_LDFLAGS = -ldl
ryuvirtualhid_CODESIGN_FLAGS = -S$(CURDIR)/entitlements.plist
include $(THEOS_MAKE_PATH)/tool.mk
