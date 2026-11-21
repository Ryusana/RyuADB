TARGET := iphone:clang:latest:15.0
ARCHS = arm64 arm64e
THEOS_PACKAGE_SCHEME = rootless
include $(THEOS)/makefiles/common.mk
TWEAK_NAME = RYUCalcProbe
RYUCalcProbe_FILES = Tweak.m
RYUCalcProbe_CFLAGS = -fobjc-arc -fblocks -Wall -Wextra -O2 -Wno-deprecated-declarations
RYUCalcProbe_FRAMEWORKS = UIKit Foundation CoreFoundation
RYUCalcProbe_LDFLAGS = -ldl
include $(THEOS_MAKE_PATH)/tweak.mk
