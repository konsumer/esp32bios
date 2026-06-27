/*
 * test_fap_load.cpp -- verify the real on-device pipeline against a real Xtensa
 * Flipper-app object: load it through elfload.cpp, resolve its furi/gui/canvas
 * imports through the actual compat shim symbol table (compat_resolve), and find
 * its FAP entry point. Everything the device does at boot except executing the
 * Xtensa code (which needs the chip).
 *
 *   test_fap_load build/hello_world.elf
 */
#include "elfload.h"
#include "compat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define EM_XTENSA 94

static void  t_log(const char* m) { printf("    %s\n", m); }
static void* t_alloc(size_t n)    { return malloc(n ? n : 1); }
static void  t_copy(void* d, const void* s, size_t n) { memcpy(d, s, n); }

/* The host's resolver: the compat shim first, then a couple of libc stragglers. */
static uintptr_t t_resolve(const char* name) {
    uintptr_t a = compat_resolve(name);
    if (a) { printf("    resolved %-26s -> shim\n", name); return a; }
    if (!strcmp(name, "memcpy")) return (uintptr_t)&memcpy;
    if (!strcmp(name, "memset")) return (uintptr_t)&memset;
    if (!strcmp(name, "strlen")) return (uintptr_t)&strlen;
    printf("    UNRESOLVED %s\n", name);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <flipper_app.elf>\n", argv[0]); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc(len);
    if (fread(buf, 1, len, f) != (size_t)len) { perror("read"); return 2; }
    fclose(f);
    printf("loaded %ld bytes from %s\n\n", len, argv[1]);

    elf_env env;
    env.alloc_exec = t_alloc; env.alloc_data = t_alloc; env.copy_exec = t_copy;
    env.resolve = t_resolve; env.log = t_log;

    const char* entry_name = (argc > 2) ? argv[2] : "hello_world_app";
    printf("resolving the Flipper app's imports against the compat shim:\n");
    elf_symbol entries[1] = { { entry_name, 0 } };
    int rc = elf_load(buf, len, &env, EM_XTENSA, entries, 1);
    printf("\nelf_load returned %d\n", rc);
    if (rc == ELF_OK) {
        printf("  FAP entry %s @ %p\n", entry_name, entries[0].addr);
        printf("\nPASS: a real Xtensa Flipper app was loaded, relocated, and had every\n");
        printf("furi/gui/canvas import bound to the shim. Execution needs the ESP32.\n");
        return 0;
    }
    printf("\nFAIL\n");
    return 1;
}
