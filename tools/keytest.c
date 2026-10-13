// keytest - probe the Pico controller service's key enable/disable API.
// usage: keytest scan | keytest set <key> <0|1> | keytest watch [seconds]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

typedef int  (*fn_scks)(int, int);          // SetControllerKeyState(key, state)
typedef int  (*fn_gcks)(void *);            // GetControllerKeyState(key_state_t*)
typedef void *(*fn_cc)(void);               // createControllerClient
typedef int  (*fn_gckd)(void *, void *);    // GetControllerKeyData(client, out)

static void hex(const unsigned char *b, int n) {
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
}

int main(int argc, char **argv) {
    void *lib = dlopen("/system/lib64/libpxrcontrollerclient.pxr.so", RTLD_LAZY);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    fn_cc  createCC = (fn_cc) dlsym(lib, "createControllerClient");
    fn_gckd GKD     = (fn_gckd)dlsym(lib, "GetControllerKeyData");
    fn_scks SCKS    = (fn_scks)dlsym(lib, "SetControllerKeyState");
    fn_gcks GCKS    = (fn_gcks)dlsym(lib, "GetControllerKeyState");
    printf("syms: create=%p GKD=%p SCKS=%p GCKS=%p\n",
           (void*)createCC, (void*)GKD, (void*)SCKS, (void*)GCKS);
    if (!createCC || !GKD || !SCKS || !GCKS) return 1;
    void *client = createCC();
    printf("client=%p\n", client);

    if (argc >= 2 && !strcmp(argv[1], "scan")) {
        for (int k = 0; k < 16; k++) {
            unsigned int buf[16];
            memset(buf, 0xAA, sizeof(buf));   // poison: reveals which fields get written
            buf[0] = (unsigned int)k;         // hypothesis: field0 = key in / state out
            int rc = GCKS(buf);
            printf("key %2d rc=%d buf:", k, rc);
            for (int i = 0; i < 6; i++) printf(" %08x", buf[i]);
            printf("\n");
            fflush(stdout);
        }
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "set")) {
        int k = atoi(argv[2]), s = atoi(argv[3]);
        int rc = SCKS(k, s);
        printf("SetControllerKeyState(%d,%d) rc=%d\n", k, s, rc);
        unsigned int buf[16];
        memset(buf, 0, sizeof(buf));
        buf[0] = (unsigned int)k;
        int rc2 = GCKS(buf);
        printf("readback rc=%d buf[0]=%u buf[1]=%d\n", rc2, buf[0], (int)buf[1]);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "watch")) {
        int secs = argc >= 3 ? atoi(argv[2]) : 30;
        unsigned char prev[2][80];
        memset(prev, 0, sizeof(prev));
        for (int i = 0; i < secs * 20; i++) {
            unsigned char cur[80];
            for (int sl = 0; sl < 2; sl++) {
                memset(cur, 0, 80);
                int rc = GKD(client, cur);
                if (rc == 0 && memcmp(cur, prev[sl], 80)) {
                    printf("slot%d: ", sl + 1);
                    hex(cur, 80);
                    printf("\n");
                    fflush(stdout);
                    memcpy(prev[sl], cur, 80);
                }
            }
            usleep(50000);
        }
        return 0;
    }
    printf("usage: keytest scan | set <key> <0|1> | watch [secs]\n");
    return 0;
}
