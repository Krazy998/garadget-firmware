# Garadget Local Firmware — Local Offline Build System (Particle P1 / Device OS 3.3.1)
ROOT_DIR      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DEVICE_OS_VER := 3.3.1
TOOLCHAIN_DIR := $(ROOT_DIR)/.toolchain
DEVICE_OS_DIR := $(TOOLCHAIN_DIR)/deviceOS/$(DEVICE_OS_VER)
GCC_ARM_BIN   := $(TOOLCHAIN_DIR)/gcc-arm/10.2.1/bin
OS_BIN_DIR    := $(TOOLCHAIN_DIR)/device-os-bins/$(DEVICE_OS_VER)
TARGET_DIR    := $(ROOT_DIR)/target
PLATFORM      := p1

.PHONY: all firmware web test clean toolchain flash flash-os ota size help

all: web firmware

help:
	@echo "Garadget Local Firmware (Particle P1 / Device OS $(DEVICE_OS_VER))"
	@echo "  make toolchain          - Download official Particle Device OS $(DEVICE_OS_VER) + GCC ARM 10.2.1"
	@echo "  make web                - Embed & gzip web UI and SoftAP portal into src/generated/"
	@echo "  make firmware           - Compile P1 firmware locally -> target/garadget.bin"
	@echo "  make test               - Build and run native C++17 host unit tests"
	@echo "  make ota HOST=<ip>      - Upload & flash target/garadget.bin over LAN HTTP OTA (optional PASS=<pw>)"
	@echo "  make flash              - Flash target/garadget.bin to P1 via USB DFU"
	@echo "  make flash-os           - Upgrade P1 bootloader + Device OS $(DEVICE_OS_VER) via USB DFU"
	@echo "  make clean              - Remove build outputs"

toolchain:
	@./scripts/setup-toolchain.sh

web:
	@python3 scripts/embed-web.py

# Particle Device OS Makefiles cannot handle spaces in directory paths.
# If ROOT_DIR contains spaces, stage the build tree cleanly in /tmp (tmpfs) and copy artifacts back.
STAGE_DIR     := /tmp/garadget-p1-stage

firmware: web
	@test -d "$(DEVICE_OS_DIR)" || (echo "Toolchain missing. Run 'make toolchain' first." && exit 1)
	@test -x "$(GCC_ARM_BIN)/arm-none-eabi-gcc" || (echo "GCC ARM missing. Run 'make toolchain' first." && exit 1)
	@mkdir -p "$(TARGET_DIR)"
	@set -e; \
	if echo "$(ROOT_DIR)" | grep -q ' '; then \
	  echo "==> Workspace path contains spaces; staging build in $(STAGE_DIR)..."; \
	  mkdir -p "$(STAGE_DIR)/app" "$(STAGE_DIR)/target"; \
	  if [ ! -f "$(STAGE_DIR)/deviceOS/.staged" ]; then \
	    rm -rf "$(STAGE_DIR)/deviceOS"; \
	    mkdir -p "$(STAGE_DIR)/deviceOS"; \
	    tar -C "$(DEVICE_OS_DIR)" --exclude=.git -cf - . | tar -C "$(STAGE_DIR)/deviceOS" -xf -; \
	    touch "$(STAGE_DIR)/deviceOS/.staged"; \
	  fi; \
	  ln -sfn "$(TOOLCHAIN_DIR)/gcc-arm/10.2.1" "$(STAGE_DIR)/gcc-arm"; \
	  ln -sfn "$(ROOT_DIR)/scripts/bin" "$(STAGE_DIR)/tools"; \
	  rm -rf "$(STAGE_DIR)/app/src"; \
	  cp -a "$(ROOT_DIR)/src" "$(ROOT_DIR)/build.mk" "$(ROOT_DIR)/project.properties" "$(STAGE_DIR)/app/"; \
	  PATH="$(STAGE_DIR)/gcc-arm/bin:$(STAGE_DIR)/tools:$(PATH)" \
	    $(MAKE) -C "$(STAGE_DIR)/deviceOS/modules/photon/user-part" all \
	    PLATFORM=$(PLATFORM) \
	    APPDIR="$(STAGE_DIR)/app" \
	    TARGET_DIR="$(STAGE_DIR)/target" \
	    TARGET_FILE="garadget" \
	    MODULAR=y \
	    PARTICLE_DEVELOP=1; \
	  cp -a "$(STAGE_DIR)/target/." "$(TARGET_DIR)/"; \
	else \
	  PATH="$(GCC_ARM_BIN):$(ROOT_DIR)/scripts/bin:$(PATH)" \
	    $(MAKE) -C "$(DEVICE_OS_DIR)/modules/photon/user-part" all \
	    PLATFORM=$(PLATFORM) \
	    APPDIR="$(ROOT_DIR)" \
	    TARGET_DIR="$(TARGET_DIR)" \
	    TARGET_FILE="garadget" \
	    MODULAR=y \
	    PARTICLE_DEVELOP=1; \
	fi
	@echo ""
	@echo "==> Build succeeded! Binary size:"
	@"$(GCC_ARM_BIN)/arm-none-eabi-size" "$(TARGET_DIR)/garadget.elf"
	@ls -lh "$(TARGET_DIR)/garadget.bin"

test:
	@mkdir -p tests/build
	g++ -std=c++17 -Wall -Wextra -Werror -O2 \
	  -Isrc \
	  tests/test_firmware.cpp \
	  src/core/json_utils.cpp \
	  src/core/timezone.cpp \
	  src/core/config.cpp \
	  src/core/door_controller.cpp \
	  src/core/alert_engine.cpp \
	  src/net/mqtt_client.cpp \
	  src/net/ha_mqtt.cpp \
	  -o tests/build/run_tests
	./tests/build/run_tests

# Upload & flash firmware wirelessly over local LAN HTTP OTA
ota: firmware
	@test -n "$(HOST)" || (echo "Usage: make ota HOST=<garadget-ip> [PASS=<admin-password>]" && exit 1)
	@echo "==> Uploading target/garadget.bin to http://$(HOST)/api/ota ..."
	@if [ -n "$(PASS)" ]; then \
	  curl -fS --progress-bar -X POST -H "Content-Type: application/octet-stream" -H "X-Auth-Token: $(PASS)" --data-binary "@$(TARGET_DIR)/garadget.bin" "http://$(HOST)/api/ota"; \
	else \
	  curl -fS --progress-bar -X POST -H "Content-Type: application/octet-stream" --data-binary "@$(TARGET_DIR)/garadget.bin" "http://$(HOST)/api/ota"; \
	fi
	@echo ""

# Flash user application to P1 in DFU mode (blinking yellow LED)
flash: firmware
	dfu-util -d 2b04:d008 -a 0 -s 0x080A0000:leave -D "$(TARGET_DIR)/garadget.bin"

# Upgrade P1 to Device OS 3.3.1 (system parts in DFU mode)
flash-os:
	@test -f "$(OS_BIN_DIR)/p1-system-part1@$(DEVICE_OS_VER).bin" || (echo "Run 'make toolchain' first." && exit 1)
	dfu-util -d 2b04:d008 -a 0 -s 0x08020000 -D "$(OS_BIN_DIR)/p1-system-part1@$(DEVICE_OS_VER).bin"
	dfu-util -d 2b04:d008 -a 0 -s 0x08060000:leave -D "$(OS_BIN_DIR)/p1-system-part2@$(DEVICE_OS_VER).bin"
	@echo "Note: If upgrading from Device OS < 2.x, also flash bootloader in Listening Mode (blinking blue):"
	@echo "  particle flash --serial $(OS_BIN_DIR)/p1-bootloader@$(DEVICE_OS_VER)+lto.bin"

clean:
	rm -rf "$(TARGET_DIR)" tests/build
