#!/system/bin/sh
# PicoXPad Service - Xbox One Controller Emulation for Pico 4
# This script runs as a Magisk service to create a virtual Xbox controller

MODDIR=${0%/*}
LOGFILE="/data/local/tmp/picoxpad.log"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> $LOGFILE
}

log "Starting PicoXPad service"

# Wait for boot to complete
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 1
done

log "Boot completed, initializing..."

# Load uinput module if not loaded
if [ ! -e /dev/uinput ]; then
    insmod /system/lib/modules/uinput.ko 2>/dev/null || modprobe uinput 2>/dev/null
    sleep 1
fi

# Find and make daemon executable (Magisk extracts ZIP without +x perms)
DAEMON=""
for path in "$MODDIR/picoxpad_daemon" "$MODDIR/arm64-v8a/picoxpad_daemon"; do
    if [ -f "$path" ]; then
        chmod 755 "$path" 2>/dev/null
        DAEMON="$path"
        break
    fi
done

if [ -z "$DAEMON" ]; then
    log "ERROR: picoxpad_daemon not found in $MODDIR or $MODDIR/arm64-v8a"
    exit 1
fi

log "Starting daemon: $DAEMON"

# Start the main daemon
exec $DAEMON >> $LOGFILE 2>&1