#!/bin/bash

# set custom OpenOCD path
# export OPENOCD_PATH=/path/to/openocd/bin
# set custom toolchain path
# export TOOLCHAIN_PATH=/path/to/toolchain/bin

# Windows firmware sync directory
WINDOWS_BIN="/mnt/f/Embed/GD32VW553/bin"

if [[ -n $1 ]]; then
    APP=$1
    if [[ -n $2 ]]; then
        USER_CMD=$2
    else
        USER_CMD="-j"
    fi
else
    APP=app
    USER_CMD="-j"
fi

if [[ ! -e $PWD/MSDK/${APP} ]]; then
    echo "build app error: $PWD/MSDK/${APP} is not exsit !!"
    exit 1
fi

# toolchain check and setup
# Check for custom toolchain path first
if [[ -n "$TOOLCHAIN_PATH" ]]; then
    if [[ -e "$TOOLCHAIN_PATH" ]]; then
        echo "Using custom toolchain path: $TOOLCHAIN_PATH"
        export PATH=$PATH:$TOOLCHAIN_PATH
    fi
fi

if ! type riscv-nuclei-elf-gcc > /dev/null 2>&1; then
    if [[ ! -e ./tools/gd32vw55x_toolchain_linux ]]; then
        if [[ -e ./tools/gd32vw55x_toolchain_linux.tar.gz00 ]]; then
            echo "Unzip gd32vw55x toolchain ......."
    #        tar xvzf ./tools/gd32vw55x_toolchain_linux.tar.gz -C ./tools
            cat ./tools/gd32vw55x_toolchain_linux.tar.gz* | tar xvz -C ./tools/
        else
            echo "Please download the gd32vw55x toolchain from the website and put it in $PATH"
            exit 1
        fi
    fi
    echo "Using toolchain path: $PWD/tools/gd32vw55x_toolchain_linux/bin"
    export PATH=$PATH:$PWD/tools/gd32vw55x_toolchain_linux/bin
else
    echo "Toolchain found in PATH: $(which riscv-nuclei-elf-gcc)"
fi

# OpenOCD check and setup
# Check for custom OpenOCD path first
if [[ -n "$OPENOCD_PATH" ]]; then
    if [[ -e "$OPENOCD_PATH" ]]; then
        echo "Using custom OpenOCD path: $OPENOCD_PATH"
        export PATH=$PATH:$OPENOCD_PATH
    fi
fi

if ! type openocd > /dev/null 2>&1; then
    if [[ ! -e ./tools/xpack-openocd-0.11.0-3_linux ]]; then
        if [[ -e ./tools/xpack-openocd-0.11.0-3_linux.tar.gz ]]; then
            echo "Unzip gd32vw55x opencod ......."
            tar xvzf ./tools/xpack-openocd-0.11.0-3_linux.tar.gz -C ./tools
        else
            echo "Please download the gd32vw55x opencod from the website and put it in $PATH"
            exit 1
        fi
    fi
    echo "Using OpenOCD path: $PWD/tools/xpack-openocd-0.11.0-3_linux/bin"
    export PATH=$PATH:$PWD/tools/xpack-openocd-0.11.0-3_linux/bin
else
    echo "OpenOCD found in PATH: $(which openocd)"
fi


if type python3 > /dev/null 2>&1; then
    PYTHON=python3
else
    echo "Please install python3: sudo apt install -y python3"
    exit 1
fi

if ! type make >/dev/null 2>&1; then
    echo "Please install make: sudo apt install -y build-essential"
    exit 1
fi

if ! type srec_cat >/dev/null 2>&1; then
    echo "Please install srecord: sudo apt install srecord"
    exit 1
fi

if ! type cmake >/dev/null 2>&1; then
    echo "Please install cmake: sudo apt install cmake"
    exit 1
fi

if [[ ! -e cmake_build ]]; then
    mkdir cmake_build
fi

cd cmake_build || exit 1


if [[ $USER_CMD = "clean" ]]; then
    echo "=========================================="
    echo "Cleaning all build artifacts..."
    echo "=========================================="

    # Clean CMake intermediate files
    rm -rf ./*

    cd ../

    # Clean MSDK build output
    rm -rf MSDK/projects/cmake/output/bin/*

    # Clean generated firmware images, keep predefined MSDK images
    find scripts/images -maxdepth 1 -type f -name "*.bin" \
    ! -name "image-all-msdk.bin" \
    ! -name "image-all-msdk-full-app.bin" \
    -delete

    # Clean MBL build output
    rm -rf MBL/project/cmake/bin/*

    echo "=========================================="
    echo "Clean completed!"
    echo "Removed:"
    echo "  - cmake_build intermediate files"
    echo "  - MSDK build artifacts"
    echo "  - Generated firmware BIN images"
    echo "  - MBL build artifacts"
    echo "=========================================="

    exit 0
else
    cmake -G "Unix Makefiles" -DAPP=${APP} -DCONFIG_BLE_FEATURE=MAX -DCONFIG_MBEDTLS_VERSION="3.6.2" -DCMAKE_TOOLCHAIN_FILE:PATH=./scripts/cmake/toolchain.cmake ..

    # Build firmware
    if ! make ${USER_CMD}; then
        echo "Build failed, skip firmware synchronization!"
        exit 1
    fi

    cd ../

    # Sync firmware to Windows
    echo "=========================================="
    echo "Synchronizing firmware to Windows..."
    echo "=========================================="

    mkdir -p "$WINDOWS_BIN" || exit 1

    # Sync build output
    rsync -av --progress \
        "$PWD/MSDK/projects/cmake/output/bin/" \
        "$WINDOWS_BIN/"

    if [[ $? -ne 0 ]]; then
        echo "Build output synchronization failed!"
        exit 1
    fi

    # Sync OTA and ALL firmware images
    rsync -av --progress \
        "$PWD/scripts/images/MBL.bin" \
        "$PWD/scripts/images/image-ota.bin" \
        "$PWD/scripts/images/image-all.bin" \
        "$WINDOWS_BIN/"

    if [[ $? -ne 0 ]]; then
        echo "Firmware image synchronization failed!"
        exit 1
    fi

    echo "=========================================="
    echo "Firmware synchronization completed!"
    echo "Windows directory: F:\\Embed\\GD32VW553\\bin"
    echo "=========================================="

    exit 0
fi

