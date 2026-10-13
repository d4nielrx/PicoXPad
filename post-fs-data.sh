#!/system/bin/sh
# PicoXPad - Post-fs-data script for Magisk
# Runs after filesystem is mounted but before boot completes

MODDIR=${0%/*}

# Create log directory
mkdir -p /data/local/tmp

# Set permissions for uinput access
chmod 666 /dev/uinput 2>/dev/null || true
chmod 666 /dev/input/uinput 2>/dev/null || true

# Load uinput module if available
insmod /system/lib/modules/uinput.ko 2>/dev/null || modprobe uinput 2>/dev/null || true

# Wait a bit for module to load
sleep 1

# Verify uinput exists
if [ ! -e /dev/uinput ] && [ ! -e /dev/input/uinput ]; then
    # Try to create uinput node
    mknod /dev/uinput c 10 223 2>/dev/null || true
    chmod 666 /dev/uinput 2>/dev/null || true
fi

# Set SELinux context for our files
chcon -R u:object_r:system_file:s0 $MODDIR 2>/dev/null || true

# Start the service
$MODDIR/service.sh &