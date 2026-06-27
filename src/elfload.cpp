/*
 * elfload.cpp -- portable ET_REL loader. See elfload.h for the contract.
 *
 * Scope on purpose: handles the relocation type that a `-mlongcalls
 * -mtext-section-literals` build actually emits for self-contained app code,
 * which is R_XTENSA_32 (absolute 32-bit, value 1 -- the same number and the same
 * S+A arithmetic as i386 R_386_32, which is why the desktop test exercises the
 * real path). Any other reloc type is reported by name so you know exactly what
 * to add. elf_inspect() lets you see the set before you ever flash a board.
 */
#include "elfload.h"
#include <string.h>
#include <stdio.h>   /* snprintf for diagnostics */

/* ---- ELF32 on-disk structures (little-endian, matches Xtensa & i386) ------- */
typedef struct { uint8_t e_ident[16]; uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } Elf32_Ehdr;
typedef struct { uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
    sh_link, sh_info, sh_addralign, sh_entsize; } Elf32_Shdr;
typedef struct { uint32_t st_name, st_value, st_size; uint8_t st_info, st_other;
    uint16_t st_shndx; } Elf32_Sym;
typedef struct { uint32_t r_offset, r_info; }                    Elf32_Rel;
typedef struct { uint32_t r_offset, r_info; int32_t r_addend; }  Elf32_Rela;

#define ET_REL          1
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHF_ALLOC       0x2
#define SHF_EXECINSTR   0x4
#define SHN_UNDEF       0
#define SHN_ABS         0xfff1
#define SHN_COMMON      0xfff2
#define EM_XTENSA       94

#define ELF32_R_SYM(i)   ((i) >> 8)
#define ELF32_R_TYPE(i)  ((i) & 0xff)

#define R_XTENSA_NONE     0
#define R_XTENSA_32       1    /* == R_386_32; *P = S + A (absolute, in literals) */
#define R_XTENSA_SLOT0_OP 20   /* PC-relative op (l32r/branch); pre-encoded by gas */

#define MAX_SECTIONS    64

static void elog(const elf_env* env, const char* m) { if (env && env->log) env->log(m); }

/* unaligned little-endian read from the image */
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const char* type_name(uint32_t t) {
    switch (t) {
        case R_XTENSA_NONE: return "R_XTENSA_NONE";
        case R_XTENSA_32:   return "R_XTENSA_32";
        case 20:            return "R_XTENSA_SLOT0_OP";
        default:            return "R_XTENSA_<other>";
    }
}

/* Validate header, return pointers to the section header table + counts.
 * Returns 0 on success. */
static int parse_header(const uint8_t* image, size_t len, uint16_t expect_machine,
                        const elf_env* env, const Elf32_Ehdr** ehp, const Elf32_Shdr** shp) {
    if (len < sizeof(Elf32_Ehdr)) { elog(env, "elf: too small"); return ELF_ERR_MAGIC; }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)image;
    if (!(eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' &&
          eh->e_ident[2] == 'L'  && eh->e_ident[3] == 'F')) { elog(env, "elf: bad magic"); return ELF_ERR_MAGIC; }
    if (eh->e_ident[4] != 1)  { elog(env, "elf: not ELFCLASS32"); return ELF_ERR_MAGIC; }   /* 32-bit */
    if (eh->e_ident[5] != 1)  { elog(env, "elf: not little-endian"); return ELF_ERR_MAGIC; }
    if (eh->e_type   != ET_REL) { elog(env, "elf: not ET_REL (build with -c)"); return ELF_ERR_MAGIC; }
    if (expect_machine && eh->e_machine != expect_machine) { elog(env, "elf: wrong machine"); return ELF_ERR_MACHINE; }
    if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shnum > MAX_SECTIONS) { elog(env, "elf: bad shdr table"); return ELF_ERR_MAGIC; }
    if (eh->e_shoff + (size_t)eh->e_shnum * sizeof(Elf32_Shdr) > len) { elog(env, "elf: shdr out of range"); return ELF_ERR_MAGIC; }
    *ehp = eh;
    *shp = (const Elf32_Shdr*)(image + eh->e_shoff);
    return ELF_OK;
}

/* Locate the (single) symbol table and its string table. */
static int find_symtab(const Elf32_Ehdr* eh, const Elf32_Shdr* sh,
                       int* symsec, int* strsec) {
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == SHT_SYMTAB) {
            *symsec = i;
            *strsec = (int)sh[i].sh_link;
            return ELF_OK;
        }
    }
    return ELF_ERR_NO_SYMTAB;
}

int elf_inspect(const uint8_t* image, size_t len, const elf_env* env) {
    const Elf32_Ehdr* eh; const Elf32_Shdr* sh;
    int rc = parse_header(image, len, 0, env, &eh, &sh);
    if (rc != ELF_OK) return rc;

    const char* shstr = (const char*)(image + sh[eh->e_shstrndx].sh_offset);
    char line[160];

    elog(env, "elf: sections (name type flags size):");
    for (int i = 0; i < eh->e_shnum; i++) {
        snprintf(line, sizeof(line), "  [%2d] %-18s type=%u flags=0x%x size=%u",
                 i, shstr + sh[i].sh_name, sh[i].sh_type, sh[i].sh_flags, sh[i].sh_size);
        elog(env, line);
    }

    int symsec, strsec;
    if (find_symtab(eh, sh, &symsec, &strsec) == ELF_OK) {
        const Elf32_Sym* syms = (const Elf32_Sym*)(image + sh[symsec].sh_offset);
        const char* str = (const char*)(image + sh[strsec].sh_offset);
        int n = (int)(sh[symsec].sh_size / sizeof(Elf32_Sym));
        for (int i = 0; i < n; i++) {
            if (syms[i].st_name && str[syms[i].st_name]) {
                const char* nm = str + syms[i].st_name;
                if (!strcmp(nm, "app_setup") || !strcmp(nm, "app_loop")) {
                    snprintf(line, sizeof(line), "  entry symbol: %s (shndx=%u value=%u)",
                             nm, syms[i].st_shndx, syms[i].st_value);
                    elog(env, line);
                }
            }
        }
    }

    /* Tally relocation types present -- the whole point of inspect. */
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_RELA && sh[i].sh_type != SHT_REL) continue;
        int entsz = (sh[i].sh_type == SHT_RELA) ? (int)sizeof(Elf32_Rela) : (int)sizeof(Elf32_Rel);
        int n = (int)(sh[i].sh_size / entsz);
        snprintf(line, sizeof(line), "elf: reloc section [%d] '%s' -> target [%u], %d entries:",
                 i, shstr + sh[i].sh_name, sh[i].sh_info, n);
        elog(env, line);
        uint32_t seen[8] = {0}; int nseen = 0;
        for (int k = 0; k < n; k++) {
            uint32_t info = rd32(image + sh[i].sh_offset + (size_t)k * entsz + 4);
            uint32_t t = ELF32_R_TYPE(info);
            int dup = 0; for (int s = 0; s < nseen; s++) if (seen[s] == t) dup = 1;
            if (!dup && nseen < 8) { seen[nseen++] = t; }
        }
        for (int s = 0; s < nseen; s++) {
            snprintf(line, sizeof(line), "    type %u (%s)", seen[s], type_name(seen[s]));
            elog(env, line);
        }
    }
    return ELF_OK;
}

int elf_load(const uint8_t* image, size_t len, const elf_env* env,
             uint16_t expect_machine, elf_app* out) {
    const Elf32_Ehdr* eh; const Elf32_Shdr* sh;
    int rc = parse_header(image, len, expect_machine, env, &eh, &sh);
    if (rc != ELF_OK) return rc;

    int symsec, strsec;
    if ((rc = find_symtab(eh, sh, &symsec, &strsec)) != ELF_OK) { elog(env, "elf: no symtab"); return rc; }
    const Elf32_Sym* syms = (const Elf32_Sym*)(image + sh[symsec].sh_offset);
    const char* str       = (const char*)(image + sh[strsec].sh_offset);
    const int   nsym      = (int)(sh[symsec].sh_size / sizeof(Elf32_Sym));

    /* 1. Place every SHF_ALLOC section into runtime memory. */
    void* base[MAX_SECTIONS] = {0};
    int nregions = 0;
    for (int i = 0; i < eh->e_shnum; i++) {
        if (!(sh[i].sh_flags & SHF_ALLOC) || sh[i].sh_size == 0) continue;
        bool exec = (sh[i].sh_flags & SHF_EXECINSTR) != 0;
        void* mem = exec ? env->alloc_exec(sh[i].sh_size) : env->alloc_data(sh[i].sh_size);
        if (!mem) { elog(env, "elf: alloc failed"); return ELF_ERR_ALLOC; }
        if (sh[i].sh_type == SHT_NOBITS) {
            memset(mem, 0, sh[i].sh_size);                       /* .bss */
        } else if (exec) {
            env->copy_exec(mem, image + sh[i].sh_offset, sh[i].sh_size);
        } else {
            memcpy(mem, image + sh[i].sh_offset, sh[i].sh_size);
        }
        base[i] = mem;
        nregions++;
    }

    /* Helper to resolve a symbol index to its runtime address. */
    /* (declared as a lambda-free static-ish closure via local function emulation) */

    /* 2. Apply relocations. */
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_RELA && sh[i].sh_type != SHT_REL) continue;
        void* target = base[sh[i].sh_info];
        if (!target) continue;                                  /* reloc for a non-loaded section */
        bool rela = (sh[i].sh_type == SHT_RELA);
        int entsz  = rela ? (int)sizeof(Elf32_Rela) : (int)sizeof(Elf32_Rel);
        int n      = (int)(sh[i].sh_size / entsz);
        for (int k = 0; k < n; k++) {
            const uint8_t* rp = image + sh[i].sh_offset + (size_t)k * entsz;
            uint32_t r_offset = rd32(rp);
            uint32_t r_info   = rd32(rp + 4);
            int32_t  addend   = rela ? (int32_t)rd32(rp + 8) : 0;
            uint32_t symidx   = ELF32_R_SYM(r_info);
            uint32_t type     = ELF32_R_TYPE(r_info);

            if (type == R_XTENSA_NONE) continue;

            /* Resolve S (symbol value). */
            uintptr_t S = 0;
            if (symidx < (uint32_t)nsym) {
                const Elf32_Sym* sym = &syms[symidx];
                if (sym->st_shndx == SHN_UNDEF) {
                    const char* nm = (sym->st_name && env->resolve) ? str + sym->st_name : 0;
                    S = nm ? env->resolve(nm) : 0;
                    if (!S) {
                        char b[96]; snprintf(b, sizeof(b), "elf: unresolved symbol '%s'",
                                             sym->st_name ? str + sym->st_name : "?");
                        elog(env, b);
                        return ELF_ERR_UNRESOLVED;
                    }
                } else if (sym->st_shndx == SHN_ABS) {
                    S = sym->st_value;
                } else if (sym->st_shndx == SHN_COMMON) {
                    elog(env, "elf: SHN_COMMON unsupported (compile with -fno-common)");
                    return ELF_ERR_RELOC;
                } else if (sym->st_shndx < MAX_SECTIONS && base[sym->st_shndx]) {
                    S = (uintptr_t)base[sym->st_shndx] + sym->st_value;
                } else {
                    elog(env, "elf: symbol in non-loaded section");
                    return ELF_ERR_RELOC;
                }
            }

            uint8_t* P = (uint8_t*)target + r_offset;
            int32_t  A = rela ? addend : (int32_t)rd32(P);       /* REL keeps addend in-place */

            switch (type) {
                case R_XTENSA_32:
                    /* 32-bit aligned store -- valid for both DRAM and ESP32 IRAM. */
                    *(uint32_t*)P = (uint32_t)(S + (uintptr_t)A);
                    break;
                case R_XTENSA_SLOT0_OP: {
                    /* l32r / branch immediates. The assembler already encoded the
                     * correct PC-relative value for the section's internal layout.
                     * We load each section as ONE contiguous block, so intra-section
                     * references stay valid and need no patching. A reference that
                     * crosses into another section is NOT pre-resolved -- we can't
                     * fix its instruction encoding here, so fail loudly. (A fully
                     * self-contained app -- all host calls via the BIOS pointer --
                     * never produces a cross-section one.) */
                    bool intra = (symidx < (uint32_t)nsym) &&
                                 (syms[symidx].st_shndx == sh[i].sh_info);
                    if (!intra) {
                        elog(env, "elf: cross-section SLOT0_OP unsupported "
                                  "(app must reach the host only via the BIOS pointer)");
                        return ELF_ERR_RELOC;
                    }
                    break;   /* no-op: layout preserved */
                }
                default: {
                    char b[80]; snprintf(b, sizeof(b), "elf: unsupported reloc type %u (%s)",
                                         type, type_name(type));
                    elog(env, b);
                    return ELF_ERR_RELOC;
                }
            }
        }
    }

    /* 3. Find the entry points. */
    out->app_setup = 0; out->app_loop = 0;
    for (int i = 0; i < nsym; i++) {
        if (!syms[i].st_name) continue;
        const char* nm = str + syms[i].st_name;
        if (syms[i].st_shndx == SHN_UNDEF || syms[i].st_shndx >= MAX_SECTIONS) continue;
        void* secbase = base[syms[i].st_shndx];
        if (!secbase) continue;
        void* addr = (uint8_t*)secbase + syms[i].st_value;
        if (!strcmp(nm, "app_setup")) out->app_setup = (void(*)(const void*))addr;
        else if (!strcmp(nm, "app_loop")) out->app_loop = (void(*)(const void*))addr;
    }
    if (!out->app_setup || !out->app_loop) { elog(env, "elf: app_setup/app_loop not found"); return ELF_ERR_NO_ENTRY; }

    /* Hand back the regions for optional later cleanup. */
    out->region_count = nregions;
    out->regions = 0;   /* caller-managed lifetime omitted for brevity */
    (void)EM_XTENSA;
    return ELF_OK;
}
