// keydump.c - probe libpxrcontrollerclient.pxr.so key-data API on Pico 4.
// Prints return values + hex of the output buffer when it changes.
// Usage: keydump [iterations] [delay_ms] [bufsize]
// Example: keydump 200 100 512   (20 seconds of sampling)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dlfcn.h>

static void hexrows(const uint8_t *b, int n) {
    for (int off = 0; off < n; off += 32) {
        printf("  %03x:", off);
        for (int i = 0; i < 32 && off + i < n; i++)
            printf(" %02x", b[off + i]);
        printf("\n");
    }
}

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 200;
    int delay_ms = argc > 2 ? atoi(argv[2]) : 100;
    int bufsz = argc > 3 ? atoi(argv[3]) : 512;
    if (bufsz < 64) bufsz = 64;
    if (bufsz > 4096) bufsz = 4096;

    void *h = dlopen("/system/lib64/libpxrcontrollerclient.pxr.so", RTLD_LAZY);
    if (!h) { printf("dlopen fail: %s\n", dlerror()); return 1; }

    void *(*createCC)(void) = (void *(*)(void))dlsym(h, "createControllerClient");
    int (*GKD)(void *, void *) = (int (*)(void *, void *))dlsym(h, "GetControllerKeyData");
    printf("createControllerClient=%p GetControllerKeyData=%p\n",
           (void *)createCC, (void *)GKD);
    if (!createCC || !GKD) { printf("missing symbols\n"); return 1; }

    void *client = createCC();
    printf("client=%p\n", client);
    sleep(2); // let service thread connect

    uint8_t *buf = malloc(bufsz);
    // Full-timeline mode: one compact line per sample, no filtering.
    // Columns: t id 04 08 0c 10 14 1c 20 24 28 2c 30 34 35 36 37 38 ret
    printf("t(s) id 04 08 0c 10 14 1c 20 24 28 2c 30 34 35 36 37 38 ret\n");
    for (int i = 0; i < iters; i++) {
        memset(buf, 0xAA, bufsz);
        int ret = GKD(client, buf);
        uint8_t id = bufsz > 0 ? buf[0] : 0;
        printf("%6.1f %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %d\n",
               i * (delay_ms / 1000.0), id,
               bufsz > 0x04 ? buf[0x04] : 0, bufsz > 0x08 ? buf[0x08] : 0,
               bufsz > 0x0c ? buf[0x0c] : 0, bufsz > 0x10 ? buf[0x10] : 0,
               bufsz > 0x14 ? buf[0x14] : 0, bufsz > 0x1c ? buf[0x1c] : 0,
               bufsz > 0x20 ? buf[0x20] : 0, bufsz > 0x24 ? buf[0x24] : 0,
               bufsz > 0x28 ? buf[0x28] : 0, bufsz > 0x2c ? buf[0x2c] : 0,
               bufsz > 0x30 ? buf[0x30] : 0, bufsz > 0x34 ? buf[0x34] : 0,
               bufsz > 0x35 ? buf[0x35] : 0, bufsz > 0x36 ? buf[0x36] : 0,
               bufsz > 0x37 ? buf[0x37] : 0, bufsz > 0x38 ? buf[0x38] : 0,
               ret);
        fflush(stdout);
        usleep((useconds_t)delay_ms * 1000);
    }
    printf("done\n");
    return 0;
}
