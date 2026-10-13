#!/system/bin/sh
# PicoXPad - Test uinput functionality
# Run via: adb push test_uinput.sh /data/local/tmp/ && adb shell sh /data/local/tmp/test_uinput.sh

echo "========================================"
echo "  PicoXPad uinput Test"
echo "========================================"
echo ""

# Check uinput
echo "1. Checking uinput device..."
if [ -e /dev/uinput ]; then
    echo "   /dev/uinput exists: OK"
    ls -la /dev/uinput
elif [ -e /dev/input/uinput ]; then
    echo "   /dev/input/uinput exists: OK"
    ls -la /dev/input/uinput
else
    echo "   uinput NOT found. Trying to load module..."
    insmod /system/lib/modules/uinput.ko 2>/dev/null || modprobe uinput 2>/dev/null
    sleep 1
    if [ -e /dev/uinput ]; then
        echo "   uinput loaded successfully"
    else
        echo "   ERROR: Could not load uinput"
        exit 1
    fi
fi

# Check permissions
echo ""
echo "2. Checking permissions..."
chmod 666 /dev/uinput 2>/dev/null || chmod 666 /dev/input/uinput 2>/dev/null
ls -la /dev/uinput 2>/dev/null || ls -la /dev/input/uinput 2>/dev/null

# Test creating a simple uinput device
echo ""
echo "3. Testing uinput device creation..."

cat > /data/local/tmp/test_uinput.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

int main() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open uinput");
        return 1;
    }
    printf("Opened uinput: fd=%d\n", fd);

    // Enable events
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    // Enable some buttons
    ioctl(fd, UI_SET_KEYBIT, BTN_A);
    ioctl(fd, UI_SET_KEYBIT, BTN_B);
    ioctl(fd, UI_SET_KEYBIT, BTN_X);
    ioctl(fd, UI_SET_KEYBIT, BTN_Y);

    // Enable axes
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);

    struct uinput_setup usetup = {0};
    struct uinput_user_dev udev = {0};

    snprintf(udev.name, UINPUT_MAX_NAME_SIZE, "Test Xbox Controller");
    udev.id.bustype = BUS_BLUETOOTH;
    udev.id.vendor = 0x045e;
    udev.id.product = 0x02e0;
    udev.id.version = 0x0100;

    udev.absmin[ABS_X] = -32768;
    udev.absmax[ABS_X] = 32767;
    udev.absmin[ABS_Y] = -32768;
    udev.absmax[ABS_Y] = 32767;

    if (write(fd, &udev, sizeof(udev)) < 0) {
        perror("write udev");
        close(fd);
        return 1;
    }

    usetup.id = udev.id;
    strncpy(usetup.name, udev.name, UINPUT_MAX_NAME_SIZE);

    if (ioctl(fd, UI_DEV_CREATE, &usetup) < 0) {
        perror("UI_DEV_CREATE");
        close(fd);
        return 1;
    }

    printf("Device created successfully!\n");
    printf("Check 'dumpsys input' for 'Test Xbox Controller'\n");
    printf("Press Enter to destroy device...\n");
    getchar();

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("Device destroyed.\n");
    return 0;
}
EOF

echo "   Compiling test program..."
cd /data/local/tmp
if command -v aarch64-linux-android29-clang++ >/dev/null; then
    aarch64-linux-android29-clang++ test_uinput.c -o test_uinput
elif command -v clang++ >/dev/null; then
    clang++ test_uinput.c -o test_uinput
else
    echo "   No compiler found. Skipping compile test."
    echo "   You can compile manually on host and push binary."
    exit 0
fi

if [ -f test_uinput ]; then
    chmod +x test_uinput
    echo "   Running test (will wait for Enter key)..."
    timeout 5 ./test_uinput || true
    rm test_uinput test_uinput.c
fi

echo ""
echo "4. Checking input system..."
dumpsys input | grep -A 5 "Test Xbox Controller" || echo "   Test device not found (may have been cleaned up)"

echo ""
echo "========================================"
echo "  Test Complete"
echo "========================================"
echo ""
echo "If device creation worked, PicoXPad should work on this device."