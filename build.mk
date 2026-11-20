# Particle Device OS build rules for Garadget Local Firmware (C++17, strict warnings)
INCLUDE_DIRS += $(APPDIR)/src

CPPSRC += $(call target_files,$(APPDIR)/src,*.cpp)
CPPSRC += $(call target_files,$(APPDIR)/src/core,*.cpp)
CPPSRC += $(call target_files,$(APPDIR)/src/hal,*.cpp)
CPPSRC += $(call target_files,$(APPDIR)/src/net,*.cpp)
CPPSRC += $(call target_files,$(APPDIR)/src/cli,*.cpp)

CFLAGS += -Wall -Wextra
CPPFLAGS += -std=gnu++17 -Wall -Wextra -Werror
