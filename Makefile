RACK_DIR := D:/Rack-SDK

# On MSYS2/MINGW, GNU make often cannot resolve "D:/..." paths.
# Prefer the POSIX-style "/d/..." path when it exists.
RACK_DIR_POSIX := /d/Rack-SDK
ifneq ($(wildcard $(RACK_DIR_POSIX)/plugin.mk),)
RACK_DIR := $(RACK_DIR_POSIX)
endif

# Compile all module sources in src/
SOURCES += $(wildcard src/*.cpp)

# Include assets in distributable package
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
