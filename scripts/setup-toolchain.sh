#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN_DIR="${ROOT_DIR}/.toolchain"
DEVICE_OS_VER="3.3.1"
DEVICE_OS_DIR="${TOOLCHAIN_DIR}/deviceOS/${DEVICE_OS_VER}"
GCC_ARM_DIR="${TOOLCHAIN_DIR}/gcc-arm/10.2.1"
OS_BIN_DIR="${TOOLCHAIN_DIR}/device-os-bins/${DEVICE_OS_VER}"

mkdir -p "${TOOLCHAIN_DIR}" "${DEVICE_OS_DIR}" "${GCC_ARM_DIR}" "${OS_BIN_DIR}"

# 1. Clone official Particle Device OS 3.3.1 source & build system (shallow + submodules)
if [ ! -f "${DEVICE_OS_DIR}/Makefile" ]; then
  rm -rf "${DEVICE_OS_DIR}"
  echo "==> Cloning Particle Device OS v${DEVICE_OS_VER}..."
  git clone --depth 1 --branch "v${DEVICE_OS_VER}" \
    --recurse-submodules --shallow-submodules \
    https://github.com/particle-iot/device-os.git "${DEVICE_OS_DIR}"
fi

# 2. Download official Device OS 3.3.1 pre-built P1 system parts + bootloader (for offline DFU flashing)
for FILE in "p1-bootloader%403.3.1%2Blto.bin:p1-bootloader@3.3.1+lto.bin" \
            "p1-system-part1%403.3.1.bin:p1-system-part1@3.3.1.bin" \
            "p1-system-part2%403.3.1.bin:p1-system-part2@3.3.1.bin"; do
  URL_NAME="${FILE%%:*}"
  OUT_NAME="${FILE##*:}"
  if [ ! -f "${OS_BIN_DIR}/${OUT_NAME}" ]; then
    echo "==> Downloading ${OUT_NAME}..."
    curl -fL --retry 3 -o "${OS_BIN_DIR}/${OUT_NAME}" \
      "https://github.com/particle-iot/device-os/releases/download/v${DEVICE_OS_VER}/${URL_NAME}"
  fi
done

# 3. Download official GNU Arm Embedded Toolchain 10-2020-q4-major (GCC 10.2.1)
if [ ! -x "${GCC_ARM_DIR}/bin/arm-none-eabi-gcc" ]; then
  ARCH="$(uname -m)"
  if [ "${ARCH}" = "x86_64" ]; then
    GCC_URL="https://developer.arm.com/-/media/Files/downloads/gnu-rm/10-2020q4/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux.tar.bz2"
  elif [ "${ARCH}" = "aarch64" ]; then
    GCC_URL="https://developer.arm.com/-/media/Files/downloads/gnu-rm/10-2020q4/gcc-arm-none-eabi-10-2020-q4-major-aarch64-linux.tar.bz2"
  else
    echo "Unsupported host architecture: ${ARCH}" >&2
    exit 1
  fi
  echo "==> Downloading GNU Arm Embedded Toolchain 10.2.1 (${ARCH})..."
  GCC_ARCHIVE="${TOOLCHAIN_DIR}/gcc-arm-10.2.1.tar.bz2"
  curl -fL --retry 3 -o "${GCC_ARCHIVE}" "${GCC_URL}"
  echo "==> Extracting GCC ARM 10.2.1..."
  tar -xjf "${GCC_ARCHIVE}" -C "${GCC_ARM_DIR}" --strip-components=1
  rm -f "${GCC_ARCHIVE}"
fi

echo "==> Toolchain ready!"
echo "    Device OS: ${DEVICE_OS_DIR}"
echo "    GCC ARM:   $("${GCC_ARM_DIR}/bin/arm-none-eabi-gcc" --version | head -n 1)"
echo "    OS Bins:   ${OS_BIN_DIR}"
