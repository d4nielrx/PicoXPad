// kstate.c - probe GetControllerKeyState(client, key_state_t*) on Pico 4.
// Polls BOTH GetControllerKeyState and GetControllerKeyData so we can
// correlate the named-key struct against the raw struct we already know.
// Usage: kstate [seconds] [delay_ms]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <dlfcn.h>

static int g_zero = 0;

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
    int delay_ms = argc > 2 ? atoi(argv[2]) : 20;
    int bufsz = 256;

    void *h = dlopen("/system/lib64/libpxrcontrollerclient.pxr.so", RTLD_LAZY);
    if (!h) { printf("dlopen fail: %s\n", dlerror()); return 1; }
    void *(*createCC)(void) = (void *(*)(void))dlsym(h, "createControllerClient");
    int (*GKS)(void *, void *) = (int (*)(void *, void *))dlsym(h, "GetControllerKeyState");
    int (*GKD)(void *, void *) = (int (*)(void *, void *))dlsym(h, "GetControllerKeyData");
    printf("createCC=%p GetControllerKeyState=%p GetControllerKeyData=%p\n",
           (void *)createCC, (void *)GKS, (void *)GKD);
    if (!createCC || !GKS) { printf("missing symbols\n"); return 1; }
    void *client = createCC();
    printf("client=%p window=%ds\n", client, secs);
    fflush(stdout);
    sleep(2);

    uint8_t prev_ks[256];
    int have_ks = 0;
    memset(prev_ks, 0, sizeof(prev_ks));

    // One-time: dump the first full key_state_t struct we ever see.
    int dumped_ks = 0, dumped_kd = 0;
    int err_ks = 0;

    for (int i = 0; i < secs * 1000 / delay_ms; i++) {
        uint8_t ks[256];
        memset(ks, 0xAA, sizeof(ks));
        int ret = GKS(client, ks);
        if (ret != 0) {
            if (!err_ks) { stamp(); printf("GKS ret=%d\n", ret); fflush(stdout); }
            err_ks = 1;
        } else {
            err_ks = 0;
            if (!dumped_ks) {
                dumped_ks = 1;
                stamp(); printf("key_state_t full dump: ");
                for (int j = 0; j < bufsz; j++) printf("%02x", ks[j]);
                printf("\n"); fflush(stdout);
            }
            if (!have_ks) {
                memcpy(prev_ks, ks, bufsz);
                have_ks = 1;
            } else {
                int changed = 0;
                for (int off = 0; off < bufsz; off++) {
                    if (ks[off] != prev_ks[off]) {
                        if (!changed) { stamp(); printf("KS"); changed = 1; }
                        printf(" %02x:%02x->%02x", off, prev_ks[off], ks[off]);
                    }
                }
                if (changed) { printf("\n"); fflush(stdout); }
                memcpy(prev_ks, ks, bufsz);
            }
        }

        // Also dump GetControllerKeyData once for cross-reference.
        if (!dumped_kd && GKD) {
            uint8_t kd[80];
            memset(kd, 0xAA, sizeof(kd));
            if (GKD(client, kd) == 0 && (kd[0] == 1 || kd[0] == 2)) {
                dumped_kd = 1;
                stamp(); printf("controller_data_t ref (id=%02x): ", kd[0]);
                for (int j = 0; j < 80; j++) printf("%02x", kd[j]);
                printf("\n"); fflush(stdout);
            }
        }
        usleep((useconds_t)delay_ms * 1000);
    }
    stamp();
    printf("KSTATE done\n");
    (void)g_zero;
    return 0;
}
