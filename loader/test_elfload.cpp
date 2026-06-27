/*
 * test_elfload.cpp -- desktop verification of the ELF loader against a REAL
 * Xtensa object. It can't execute Xtensa code on x86, but it exercises every
 * other path: header validation, section placement, symbol resolution, the
 * R_XTENSA_32 relocation arithmetic, the SLOT0_OP intra-section rule, and
 * entry-point discovery -- on the exact bytes that would be flashed.
 *
 *   test_elfload <app.elf>
 */
#include "elfload.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define EM_XTENSA 94

static void  t_log(const char* m)          { printf("%s\n", m); }
static void* t_alloc(size_t n)             { return malloc(n ? n : 1); }
static void  t_copy(void* d, const void* s, size_t n) { memcpy(d, s, n); }
static uintptr_t t_resolve(const char* nm) {
    /* Stand in for the host symbol table (memcpy/memset stragglers, etc.). */
    printf("    (host asked to resolve external symbol '%s')\n", nm);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <app.elf>\n", argv[0]); return 2; }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc(len);
    if (fread(buf, 1, len, f) != (size_t)len) { perror("read"); return 2; }
    fclose(f);
    printf("loaded %ld bytes from %s\n\n", len, argv[1]);

    elf_env env;
    env.alloc_exec = t_alloc;
    env.alloc_data = t_alloc;
    env.copy_exec  = t_copy;
    env.resolve    = t_resolve;
    env.log        = t_log;

    printf("===== elf_inspect =====\n");
    elf_inspect(buf, len, &env);

    printf("\n===== elf_load =====\n");
    elf_symbol entries[2] = { { "app_setup", 0 }, { "app_loop", 0 } };
    int rc = elf_load(buf, len, &env, EM_XTENSA, entries, 2);
    printf("elf_load returned %d\n", rc);
    if (rc == ELF_OK) {
        printf("  app_setup @ %p\n", entries[0].addr);
        printf("  app_loop  @ %p\n", entries[1].addr);
        printf("\nPASS: real Xtensa object fully parsed, placed, and relocated.\n");
        printf("(Execution itself requires the ESP32 -- see host_elf.cpp.)\n");
    } else {
        printf("\nFAIL\n");
    }
    free(buf);
    return rc == ELF_OK ? 0 : 1;
}
