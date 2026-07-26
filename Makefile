RACK_DIR ?= /d/Rack-SDK

# Compile all module sources in src/
SOURCES += $(wildcard src/*.cpp)

# Include assets in distributable package
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
