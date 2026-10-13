#!/system/bin/sh
# PicoXPad - On-device installation script
# Run via: adb push install.sh /data/local/tmp/ && adb shell sh /data/local/tmp/install.sh

set -e

ZIP_URL="https://github.com/your-repo/PicoXPad/releases/download/v1.0/PicoXPad-v1.0.zip"
ZIP_PATH="/sdcard/Download/PicoXPad-v1.0.zip"
MODDIR="/data/adb/modules/PicoXPad"

echo "========================================"
echo "  PicoXPad Installer"
echo "========================================"
echo ""

# Check root
if [ "$(id -u)" != "0" ]; then
    echo "ERROR: Root required. Run with 'su' or via Magisk."
    exit 1
fi

# Check Magisk
if [ ! -d "/data/adb/magisk" ]; then
    echo "ERROR: Magisk not found. Please install Magisk first."
    exit 1
fi

MAGISK_VER=$(magisk -v 2>/dev/null | head -1)
echo "Magisk: $MAGISK_VER"

# Download or use local ZIP
if [ -f "$ZIP_PATH" ]; then
    echo "Using local ZIP: $ZIP_PATH"
else
    echo "Downloading module..."
    if command -v curl >/dev/null; then
        curl -L -o "$ZIP_PATH" "$ZIP_URL" || {
            echo "Download failed. Please manually transfer ZIP to $ZIP_PATH"
            exit 1
        }
    elif command -v wget >/dev/null; then
        wget -O "$ZIP_PATH" "$ZIP_URL" || {
            echo "Download failed. Please manually transfer ZIP to $ZIP_PATH"
            exit 1
        }
    else
        echo "Neither curl nor wget available. Please manually transfer ZIP to $ZIP_PATH"
        exit 1
    fi
fi

# Install via Magisk
echo "Installing module..."
magisk --install-module "$ZIP_PATH"

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "  Installation successful!"
    echo "========================================"
    echo ""
    echo "Please REBOOT your Pico 4 now."
    echo ""
    echo "After reboot:"
    echo "  1. Hold BOTH GRIPS (~0.5s) to toggle Xbox mode on/off"
    echo "  2. Status: cat /data/local/tmp/picoxpad_status"
    echo "  3. Check logs: logcat -s PicoXPad"
    echo ""
else
    echo "ERROR: Module installation failed"
    exit 1
fi