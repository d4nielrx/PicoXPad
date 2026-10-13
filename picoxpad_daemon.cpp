// picoxpad_daemon.cpp - Pico 4 (A8110) -> Xbox One gamepad bridge via uinput.
//
// Controller input is read through the on-device Pico client library using
// its real C API (verified against the library's exported symbols):
//     createControllerClient()  -> service singleton (PvrContorlServiceClient)
//     GetControllerKeyData(client, controller_data_t *out)  -> 0 on success
// Each call returns ONE controller's 80-byte struct; calls alternate slots.
//   out[0x00] : slot id (0x01 = left, 0x02 = right; 0x00 = no data/asleep)
//
// Verified controller_data_t layout (calibrated on-device, Oct 2026 - each
// button pressed individually and matched to its byte):
//   slot id byte 0x00: 0x01 = LEFT controller, 0x02 = RIGHT controller
//   0x0C : face primary click  (X on left / A on right)
//   0x10 : face secondary click(Y on left / B on right)
//   0x14 : grip click          0x35 : grip analog (0x00..0xFF)
//   0x1C : trigger click       0x34 : trigger analog (rest 0x02..03)
//   0x36 : stick VERTICAL   (0x00=down,  0x80 center, 0xFF=up)
//   0x37 : stick HORIZONTAL (0x00=right, 0x80 center, 0xFF=left)
//   NOTE: transposed + reversed vs ABS convention -> emit negated (see below)
//   0x04 : stick click (L3/R3)
//   0x08 : small button (menu on left / right-hand small button)
//   0x38 : battery
// Capacitive TOUCH bytes - flicker while merely holding the controller,
// must never be mapped: 0x18 0x20 0x24 0x28 0x2C 0x30
//
// IMPORTANT: we fake VID/PID 045e:02e0, so Android translates our emitted
// key/axis codes via /system/usr/keylayout/Vendor_045e_Product_02e0.kl
// (verified with dumpsys input). We must emit EXACTLY the codes that file
// expects, otherwise buttons land on the wrong Xbox function:
//   BUTTON_A  <- 0x130 (BTN_SOUTH)   BUTTON_B  <- 0x131 (BTN_EAST)
//   BUTTON_X  <- 0x132 (BTN_C)       BUTTON_Y  <- 0x133 (BTN_NORTH)
//   BUTTON_L1 <- 0x134 (BTN_WEST)    BUTTON_R1 <- 0x135 (BTN_Z)
//   BUTTON_START (hamburger) <- 0x137 (BTN_TR)   BUTTON_SELECT (View) <- 0x136
//   BUTTON_THUMBL <- 0x138 (BTN_TL2) BUTTON_THUMBR <- 0x139 (BTN_TR2)
//   BUTTON_MODE (Xbox guide) <- 0x8b
//   axis 0x02 ABS_Z->LTRIGGER   0x05 ABS_RZ->RTRIGGER
//   axis 0x00/0x01 ABS_X/ABS_Y (left stick)  0x03/0x04 ABS_RX/ABS_RY -> Z/RZ
// Stick bytes are TRANSPOSED (0x36=vertical, 0x37=horizontal) and one is
// reversed vs ABS convention (calibrated via gamepad tester, Oct 13):
// emit with byte swap + negation, see scale_stick().
//
// Toggle: hold BOTH grips ~0.5s flips emulation OFF<->ON (default OFF).
// Status is published to /data/local/tmp/picoxpad_status ("ON"/"OFF").

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <dlfcn.h>
#include <android/log.h>

#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "PicoXPad", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, "PicoXPad", fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "PicoXPad", fmt, ##__VA_ARGS__)

// ---- verified controller_data_t offsets ----
#define CD_SIZE        80
#define CD_SLOT        0x00  // 1 = left, 2 = right
#define CD_STICKCLICK  0x04  // L3 / R3
#define CD_AX          0x0C  // X (left) / A (right)
#define CD_BY          0x10  // Y (left) / B (right)
#define CD_GRIP_CLICK  0x14
#define CD_TRIG_CLICK  0x1C
#define CD_SMALL       0x08  // menu (left) / small button (right)
#define CD_TRIG_ANA    0x34
#define CD_GRIP_ANA    0x35
#define CD_STICK_Y     0x36  // vertical   (00=down,  80=center, FF=up)
#define CD_STICK_X     0x37  // horizontal (00=left,  80=center, FF=right)

// Emitted key codes - chosen to match Vendor_045e_Product_02e0.kl exactly.
#define K_A      BTN_SOUTH  // 0x130
#define K_B      BTN_EAST   // 0x131
#define K_X      BTN_C      // 0x132 (!) .kl maps BTN_C -> BUTTON_X
#define K_Y      BTN_NORTH  // 0x133 (!) .kl maps NORTH -> BUTTON_Y
#define K_LB     BTN_WEST   // 0x134 -> BUTTON_L1
#define K_RB     BTN_Z      // 0x135 -> BUTTON_R1
#define K_START  BTN_TR     // 0x137 -> BUTTON_START (hamburger)
#define K_L3     BTN_TL2    // 0x138 -> BUTTON_THUMBL
#define K_R3     BTN_TR2    // 0x139 -> BUTTON_THUMBR
#define K_GUIDE  0x8b       // -> BUTTON_MODE (Xbox guide)
static const int kEmitKeys[] = { K_A, K_B, K_X, K_Y, K_LB, K_RB,
                                 K_START, K_L3, K_R3, K_GUIDE };

#define GRIP_ANA_THRESHOLD 0x80
#define TOGGLE_HOLD_MS     500
#define SLOT_STALE_MS      300
#define POLL_US            10000  // 10ms -> ~50Hz per slot

static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

typedef struct {
    uint8_t raw[CD_SIZE];
    uint64_t last_update;
    int seen;  // ever received valid data
} slot_state_t;

static int grip_active(const slot_state_t *s) {
    if (!s->seen) return 0;
    if (now_ms() - s->last_update > SLOT_STALE_MS) return 0;
    return s->raw[CD_GRIP_CLICK] || s->raw[CD_GRIP_ANA] >= GRIP_ANA_THRESHOLD;
}

static void write_status(int enabled) {
    FILE *f = fopen("/data/local/tmp/picoxpad_status", "w");
    if (f) {
        fprintf(f, "%s", enabled ? "ON" : "OFF");
        fclose(f);
    }
}

static void emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
        LOGW("uinput write failed: %s", strerror(errno));
}

static void emit_syn(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }

// Raw stick byte -> ABS via NEGATED scale: v=0 -> +32767, v=128 -> -128
// (center, inside flat), v=255 -> -32767. Vertical (0x36) is reversed
// (00=down) -> use directly for ABS_Y; horizontal (0x37) is 00=left ->
// negate it for ABS_X.
static int scale_stick(uint8_t v) {
    int x = -((int)v * 257 - 32768);
    return x > 32767 ? 32767 : x;  // clamp: v=0 would give +32768
}

static void emit_neutral_frame(int fd) {
    for (size_t i = 0; i < sizeof(kEmitKeys) / sizeof(kEmitKeys[0]); i++)
        emit(fd, EV_KEY, kEmitKeys[i], 0);
    emit(fd, EV_ABS, ABS_X, 0);
    emit(fd, EV_ABS, ABS_Y, 0);
    emit(fd, EV_ABS, ABS_RX, 0);
    emit(fd, EV_ABS, ABS_RY, 0);
    emit(fd, EV_ABS, ABS_Z, 0);
    emit(fd, EV_ABS, ABS_RZ, 0);
    emit(fd, EV_ABS, ABS_HAT0X, 0);
    emit(fd, EV_ABS, ABS_HAT0Y, 0);
    emit_syn(fd);
}

// Map one cached slot to one side of the Xbox pad (is_left = slot 01).
// Fully calibrated mapping - each byte below was pressed individually.
static void emit_slot(int fd, const slot_state_t *s, int is_left) {
    int stale = !s->seen || (now_ms() - s->last_update > SLOT_STALE_MS);
    const uint8_t *r = stale ? NULL : s->raw;

    uint8_t ax    = stale ? 0 : r[CD_AX];          // X (L) / A (R)
    uint8_t by    = stale ? 0 : r[CD_BY];          // Y (L) / B (R)
    uint8_t grip  = stale ? 0 : (r[CD_GRIP_CLICK] || r[CD_GRIP_ANA] >= GRIP_ANA_THRESHOLD);
    uint8_t trig  = stale ? 0 : r[CD_TRIG_ANA];
    uint8_t stick = stale ? 0 : r[CD_STICKCLICK];  // L3 / R3
    uint8_t small = stale ? 0 : r[CD_SMALL];       // menu (L) / home (R)
    if (trig < 8) trig = 0;                        // analog rest is 0x02..03
    int sx = stale ? 0 : -scale_stick(r[CD_STICK_X]); // horizontal (raw 00=left -> negate)
    int sy = stale ? 0 : scale_stick(r[CD_STICK_Y]);  // vertical

    if (is_left) {
        emit(fd, EV_KEY, K_X, ax ? 1 : 0);         // Pico X -> BUTTON_X
        emit(fd, EV_KEY, K_Y, by ? 1 : 0);         // Pico Y -> BUTTON_Y
        emit(fd, EV_KEY, K_LB, grip ? 1 : 0);      // grip -> LB
        emit(fd, EV_KEY, K_L3, stick ? 1 : 0);     // stick click -> THUMBL
        emit(fd, EV_KEY, K_START, small ? 1 : 0);  // menu -> START (hamburger)
        emit(fd, EV_ABS, ABS_X, sx);
        emit(fd, EV_ABS, ABS_Y, sy);
        emit(fd, EV_ABS, ABS_Z, trig * 4);         // LT 0..1020
    } else {
        emit(fd, EV_KEY, K_A, ax ? 1 : 0);         // Pico A -> BUTTON_A
        emit(fd, EV_KEY, K_B, by ? 1 : 0);         // Pico B -> BUTTON_B
        emit(fd, EV_KEY, K_RB, grip ? 1 : 0);      // grip -> RB
        emit(fd, EV_KEY, K_R3, stick ? 1 : 0);     // stick click -> THUMBR
        emit(fd, EV_KEY, K_GUIDE, small ? 1 : 0);  // home -> Xbox guide
        emit(fd, EV_ABS, ABS_RX, sx);
        emit(fd, EV_ABS, ABS_RY, sy);
        emit(fd, EV_ABS, ABS_RZ, trig * 4);        // RT 0..1020
    }
}

// Only the capacitive TOUCH bytes remain unmapped - logged on change so any
// future behavior (e.g. new firmware) can be diagnosed.
static const uint8_t kUnmapped[] = { 0x18, 0x20, 0x24, 0x28, 0x2C, 0x30 };

// Per-slot last-logged snapshot of unmapped bytes + primary (to show context).
static uint8_t g_logged[2][CD_SIZE];

static void log_unknown_keys(const slot_state_t *slot, int idx) {
    if (!slot->seen || (now_ms() - slot->last_update > SLOT_STALE_MS))
        return;
    int changed = 0;
    for (size_t i = 0; i < sizeof(kUnmapped); i++) {
        uint8_t o = kUnmapped[i];
        if (slot->raw[o] != g_logged[idx][o]) { changed = 1; break; }
    }
    if (!changed) return;
    memcpy(g_logged[idx], slot->raw, CD_SIZE);
    char line[CD_SIZE * 2 + 1];
    for (int i = 0; i < CD_SIZE; i++) sprintf(line + i * 2, "%02x", slot->raw[i]);
    LOGI("slot%d keys: %s", idx + 1, line);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    LOGI("PicoXPad starting (uinput Xbox 045e:02e0, grip-toggle)");

    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        LOGE("open /dev/uinput: %s", strerror(errno));
        return 1;
    }
    // Full setup on the same fd we emit through (see setup below, inlined
    // to keep the emitting fd valid).
    {
        ioctl(ufd, UI_SET_EVBIT, EV_KEY);
        ioctl(ufd, UI_SET_EVBIT, EV_ABS);
        for (size_t i = 0; i < sizeof(kEmitKeys) / sizeof(kEmitKeys[0]); i++)
            ioctl(ufd, UI_SET_KEYBIT, kEmitKeys[i]);
        int axes[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ,
                       ABS_HAT0X, ABS_HAT0Y };
        for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++)
            ioctl(ufd, UI_SET_ABSBIT, axes[i]);
        struct uinput_abs_setup a;
        memset(&a, 0, sizeof(a));
        int sticks[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
        for (size_t i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++) {
            a.code = sticks[i];
            a.absinfo.minimum = -32768;
            a.absinfo.maximum = 32767;
            a.absinfo.fuzz = 16;
            a.absinfo.flat = 128;
            ioctl(ufd, UI_ABS_SETUP, &a);
        }
        int trigs[] = { ABS_Z, ABS_RZ };
        for (size_t i = 0; i < sizeof(trigs) / sizeof(trigs[0]); i++) {
            a.code = trigs[i];
            a.absinfo.minimum = 0;
            a.absinfo.maximum = 1023;
            a.absinfo.fuzz = 4;
            a.absinfo.flat = 8;
            ioctl(ufd, UI_ABS_SETUP, &a);
        }
        int hats[] = { ABS_HAT0X, ABS_HAT0Y };
        for (size_t i = 0; i < sizeof(hats) / sizeof(hats[0]); i++) {
            a.code = hats[i];
            a.absinfo.minimum = -1;
            a.absinfo.maximum = 1;
            ioctl(ufd, UI_ABS_SETUP, &a);
        }
        struct uinput_setup usetup;
        memset(&usetup, 0, sizeof(usetup));
        usetup.id.bustype = BUS_BLUETOOTH;
        usetup.id.vendor = 0x045e;
        usetup.id.product = 0x02e0;
        usetup.id.version = 0x0100;
        snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "Xbox Wireless Controller (PicoXPad)");
        if (ioctl(ufd, UI_DEV_SETUP, &usetup) < 0 || ioctl(ufd, UI_DEV_CREATE) < 0) {
            LOGE("uinput setup/create failed: %s", strerror(errno));
            close(ufd);
            return 1;
        }
    }
    LOGI("Xbox Wireless Controller (PicoXPad) created");

    void *lib = dlopen("/system/lib64/libpxrcontrollerclient.pxr.so", RTLD_LAZY);
    void *(*createCC)(void) = lib ? (void *(*)(void))dlsym(lib, "createControllerClient") : NULL;
    int (*GKD)(void *, void *) = lib ? (int (*)(void *, void *))dlsym(lib, "GetControllerKeyData") : NULL;
    void *client = (createCC) ? createCC() : NULL;
    LOGI("Pico client: lib=%p create=%p GetKeyData=%p client=%p",
         lib, (void *)createCC, (void *)GKD, client);
    if (!client || !GKD)
        LOGW("Pico client unavailable; will retry. Emulation stays neutral until data arrives.");

    slot_state_t slot[2];
    memset(slot, 0, sizeof(slot));
    memset(g_logged, 0, sizeof(g_logged));

    int enabled = 0;
    write_status(enabled);
    LOGI("Emulation OFF (default). Hold BOTH GRIPS ~0.5s to toggle. Status: /data/local/tmp/picoxpad_status");
    emit_neutral_frame(ufd);

    uint64_t both_since = 0;
    int toggle_armed = 0;
    int err_streak = 0;

    uint8_t raw[CD_SIZE];
    for (;;) {
        if (client && GKD) {
            memset(raw, 0, sizeof(raw));
            int rc = GKD(client, raw);
            if (rc == 0 && (raw[CD_SLOT] == 1 || raw[CD_SLOT] == 2)) {
                int s = raw[CD_SLOT] - 1;
                memcpy(slot[s].raw, raw, CD_SIZE);
                slot[s].last_update = now_ms();
                slot[s].seen = 1;
                err_streak = 0;
            } else if (rc != 0) {
                if (++err_streak == 1 || err_streak % 500 == 0)
                    LOGW("GetControllerKeyData rc=%d (streak %d)", rc, err_streak);
            }
        } else {
            // Retry binding (the controller service may start after us).
            if (!lib) {
                lib = dlopen("/system/lib64/libpxrcontrollerclient.pxr.so", RTLD_LAZY);
                if (lib) {
                    createCC = (void *(*)(void))dlsym(lib, "createControllerClient");
                    GKD = (int (*)(void *, void *))dlsym(lib, "GetControllerKeyData");
                }
            }
            if (lib && createCC && GKD && !client) {
                client = createCC();
                if (client) LOGI("Pico client bound on retry: %p", client);
            }
            usleep(500000);
            continue;
        }

        int gl = grip_active(&slot[0]);
        int gr = grip_active(&slot[1]);
        uint64_t t = now_ms();
        if (gl && gr) {
            if (!both_since) both_since = t;
            if (!toggle_armed && t - both_since >= TOGGLE_HOLD_MS) {
                enabled = !enabled;
                toggle_armed = 1;
                write_status(enabled);
                LOGI("Emulation %s (both grips held)", enabled ? "ENABLED" : "DISABLED");
                if (!enabled) emit_neutral_frame(ufd);
            }
        } else {
            both_since = 0;
            toggle_armed = 0;
        }

        if (enabled) {
            emit_slot(ufd, &slot[0], 1);
            emit_slot(ufd, &slot[1], 0);
            emit_syn(ufd);
        }
        log_unknown_keys(&slot[0], 0);
        log_unknown_keys(&slot[1], 1);

        usleep(POLL_US);
    }

    return 0;
}
