/*
 * vector_demo.cpp -- minimal proof of the published-pointer discovery flow,
 * with no dlopen and no hardware. Build/run:
 *
 *   g++ -std=c++17 -Iinclude examples/vector_demo.cpp src/bios_vector.cpp -o /tmp/vdemo && /tmp/vdemo
 *
 * `host_side()` builds a table and publishes it. `guest_side()` stands in for a
 * separately loaded app: it is handed nothing, it only knows bios_vector_discover().
 */
#include "bios_vector.h"
#include <cstdio>

/* ---- a throwaway host that fills just enough of the table to call ---------- */
static void  d_log(const char* m) { printf("    [bios] %s\n", m); }
static int16_t d_w(void) { return 128; }
static int16_t d_h(void) { return 64; }

static BiosTable g_table;

static void host_side(void)
{
    printf("host : building jump table, publishing pointer to the slot\n");
    g_table.magic         = BIOS_MAGIC;
    g_table.version       = BIOS_VERSION;
    g_table.size          = sizeof(BiosTable);
    g_table.log           = d_log;
    g_table.display_width = d_w;
    g_table.display_height= d_h;
    bios_vector_publish(&g_table);
}

static void guest_side(void)
{
    printf("guest: I was linked against nothing. Looking up the BIOS...\n");
    const BiosTable* bios = bios_vector_discover();
    if (!bios) { printf("guest: no BIOS published!\n"); return; }
    printf("guest: found BIOS v%u, %dx%d display. Calling log() through it:\n",
           bios->version, bios->display_width(), bios->display_height());
    bios->log("hello from a guest that discovered me");
}

int main(void)
{
    printf("--- before publish ---\n");
    guest_side();              /* should fail cleanly: slot empty */
    printf("--- host boots ---\n");
    host_side();
    printf("--- guest runs ---\n");
    guest_side();              /* should succeed */
    return 0;
}
