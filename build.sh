#!/bin/bash
# Build script for PicoXPad daemon
# Can be run on device (with NDK) or on host machine with NDK installed

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUT_DIR="${SCRIPT_DIR}/out"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Detect NDK
find_ndk() {
    if [ -n "$ANDROID_NDK_HOME" ] && [ -d "$ANDROID_NDK_HOME" ]; then
        echo "$ANDROID_NDK_HOME"
        return 0
    fi
    
    if [ -n "$ANDROID_NDK_ROOT" ] && [ -d "$ANDROID_NDK_ROOT" ]; then
        echo "$ANDROID_NDK_ROOT"
        return 0
    fi
    
    # Common locations
    for path in \
        "$HOME/Android/Sdk/ndk" \
        "$HOME/Library/Android/sdk/ndk" \
        "/opt/android-ndk" \
        "/usr/local/android-ndk" \
        "C:/Android/Sdk/ndk" \
        "C:/Users/*/AppData/Local/Android/Sdk/ndk"; do
        if [ -d "$path" ]; then
            latest=$(ls -1 "$path" | sort -V | tail -1)
            if [ -n "$latest" ]; then
                echo "$path/$latest"
                return 0
            fi
        fi
    done
    
    return 1
}

NDK_PATH=$(find_ndk)
if [ -z "$NDK_PATH" ]; then
    log_error "Android NDK not found. Set ANDROID_NDK_HOME or install NDK."
    log_info "Download from: https://developer.android.com/ndk/downloads"
    exit 1
fi

log_info "Using NDK: $NDK_PATH"

# Build for ARM64 (Pico 4 is ARM64)
API_LEVEL=29
ARCH=arm64
ABI=arm64-v8a
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$(uname -s | tr '[:upper:]' '[:lower:]')-x86_64"
CC="$TOOLCHAIN/bin/aarch64-linux-android${API_LEVEL}-clang"
CXX="$TOOLCHAIN/bin/aarch64-linux-android${API_LEVEL}-clang++"

if [ ! -f "$CC" ]; then
    log_error "Compiler not found: $CC"
    log_error "Make sure NDK version supports API level $API_LEVEL"
    exit 1
fi

log_info "Compiler: $CC"
log_info "Target: $ABI (API $API_LEVEL)"

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Compile
log_info "Compiling picoxpad_daemon..."
$CXX -std=c++17 -O2 -Wall -Wextra -D_GNU_SOURCE \
    -I"$TOOLCHAIN/sysroot/usr/include" \
    -I"$TOOLCHAIN/sysroot/usr/include/aarch64-linux-android" \
    "$SCRIPT_DIR/picoxpad_daemon.cpp" \
    -o picoxpad_daemon \
    -llog -lpthread -landroid \
    -Wl,--build-id=sha1

if [ $? -ne 0 ]; then
    log_error "Compilation failed"
    exit 1
fi

# Strip binary to reduce size
"$TOOLCHAIN/bin/llvm-strip" picoxpad_daemon 2>/dev/null || true

# Verify binary
file picoxpad_daemon

# Create output directory
mkdir -p "$OUT_DIR/$ABI"
cp picoxpad_daemon "$OUT_DIR/$ABI/"

log_info "Build successful!"
log_info "Binary: $OUT_DIR/$ABI/picoxpad_daemon"

# Also create a module zip
create_module_zip() {
    log_info "Creating Magisk module ZIP..."
    cd "$SCRIPT_DIR"

    VERSION=$(sed -n 's/^version=//p' module.prop)
    ZIP_NAME="PicoXPad-v${VERSION}.zip"
    rm -f "$ZIP_NAME"

    # Create module structure in temp dir.
    # NOTE: the binary MUST sit at the ZIP ROOT - subdirectories do not
    # extract properly on this device's Magisk.
    TMP_DIR=$(mktemp -d)
    MODULE_DIR="$TMP_DIR/PicoXPad"
    mkdir -p "$MODULE_DIR"

    # Copy files
    cp module.prop "$MODULE_DIR/"
    cp service.sh "$MODULE_DIR/"
    cp post-fs-data.sh "$MODULE_DIR/"
    cp sepolicy.rule "$MODULE_DIR/"
    cp "$OUT_DIR/$ABI/picoxpad_daemon" "$MODULE_DIR/"

    # Make scripts executable
    chmod +x "$MODULE_DIR/service.sh"
    chmod +x "$MODULE_DIR/post-fs-data.sh"
    chmod +x "$MODULE_DIR/picoxpad_daemon"

    # Create ZIP
    cd "$TMP_DIR"
    zip -r "$SCRIPT_DIR/$ZIP_NAME" PicoXPad/

    # Cleanup
    rm -rf "$TMP_DIR"

    log_info "Module ZIP created: $SCRIPT_DIR/$ZIP_NAME"
}

create_module_zip