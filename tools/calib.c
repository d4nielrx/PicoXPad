// calib.c - change-only calibration logger for Pico 4 controller_data_t.
// Polls GetControllerKeyData and prints ONE line per byte change:
//   HH:MM:SS.mmm slot=0N off=0xXX old->new
// So a single button press produces a small, unambiguous fingerprint.
// Usage: calib [seconds] [delay_ms]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <dlfcn.h>

#define CD_SIZE 80

static int g_zero = 0;

static double now_s(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void stamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    time_t sec = (time_t)tv.tv_sec;
    localtime_r(&sec, &tm);
    printf("%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec,
           (int)(tv.tv_usec / 1000));
}

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    int delay_ms = argc > 2 ? atoi(argv[2]) : 10;

    void *h = dlopen("/system/lib64/libpxrcontrollerclient.pxr.so", RTLD_LAZY);
    if (!h) { printf("dlopen fail: %s\n", dlerror()); return 1; }
    void *(*createCC)(void) = (void *(*)(void))dlsym(h, "createControllerClient");
    int (*GKD)(void *, void *) = (int (*)(void *, void *))dlsym(h, "GetControllerKeyData");
    if (!createCC || !GKD) { printf("missing symbols\n"); return 1; }
    void *client = createCC();
    printf("CALIB start client=%p window=%ds delay=%dms\n", client, secs, delay_ms);
    fflush(stdout);
    sleep(2);

    uint8_t prev[2][CD_SIZE];
    int have[2] = {0, 0};
    memset(prev, 0, sizeof(prev));

    double t0 = now_s();
    int last_err = 0;
    while (now_s() - t0 < secs) {
        uint8_t buf[CD_SIZE];
        memset(buf, 0xAA, sizeof(buf));
        int ret = GKD(client, buf);
        if (ret != 0) {
            if (!last_err) { stamp(); printf("ret=%d\n", ret); fflush(stdout); }
            last_err = 1;
        } else {
            last_err = 0;
            int id = buf[0];
            if (id == 1 || id == 2) {
                g_zero = 0;
                int s = id - 1;
                if (!have[s]) {
                    memcpy(prev[s], buf, CD_SIZE);
                    have[s] = 1;
                    stamp();
                    printf("slot%02d first sample\n", id);
                    fflush(stdout);
                } else {
                    int changed = 0;
                    for (int off = 1; off < CD_SIZE; off++) {
                        if (buf[off] != prev[s][off]) {
                            if (!changed) { stamp(); printf("slot%02d", id); changed = 1; }
                            printf(" %02x:%02x->%02x", off, prev[s][off], buf[off]);
                        }
                    }
                    if (changed) { printf("\n"); fflush(stdout); }
                    memcpy(prev[s], buf, CD_SIZE);
                }
            } else if (have[0] || have[1]) {
                // Asleep/empty struct: note transition once per streak.
                if (!g_zero) { stamp(); printf("all zeros (asleep?)\n"); fflush(stdout); }
                g_zero = 1;
                have[0] = have[1] = 0;
            } else {
                g_zero = 0;
            }
        }
        usleep((useconds_t)delay_ms * 1000);
    }
    stamp();
    printf("CALIB done\n");
    return 0;
}
