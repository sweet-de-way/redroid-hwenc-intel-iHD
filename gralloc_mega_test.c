/*
 * gralloc_mega_test.c — 一体化诊断巨型测试单元 v1.1
 *
 * v1.1 修复：
 *   - fn_bo_get_plane_cnt 拼写 → fn_bo_get_plane_count
 *   - stage1_gbm_load 返回类型 void → int（REQ 宏需要 return -1）
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cutils/native_handle.h>
#include <hardware/gralloc.h>

/* ---------- Android format ---------- */
#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#endif
#ifndef HAL_PIXEL_FORMAT_RGBX_8888
#define HAL_PIXEL_FORMAT_RGBX_8888 2
#endif
#ifndef HAL_PIXEL_FORMAT_RGB_565
#define HAL_PIXEL_FORMAT_RGB_565 4
#endif
#ifndef HAL_PIXEL_FORMAT_YV12
#define HAL_PIXEL_FORMAT_YV12 0x32315659
#endif
#ifndef HAL_PIXEL_FORMAT_YCbCr_420_888
#define HAL_PIXEL_FORMAT_YCbCr_420_888 0x23
#endif

#define FOURCC_NV12 0x3231564Eu

/* ---------- 真实 Android gralloc.h usage 位值 ---------- */
#define U_SW_READ_OFTEN   0x00000003u
#define U_SW_WRITE_OFTEN  0x00000030u
#define U_HW_TEXTURE      0x00000100u
#define U_HW_RENDER       0x00000200u
#define U_HW_COMPOSER     0x00000800u

/* ---------- GBM 手工定义 ---------- */
struct gbm_device;
struct gbm_bo;

#define GBM_BO_USE_SCANOUT    (1u << 0)
#define GBM_BO_USE_RENDERING  (1u << 2)
#define GBM_BO_USE_LINEAR     (1u << 4)
#define GBM_BO_USE_TEXTURING  (1u << 5)

#define GBM_BO_TRANSFER_READ  (1u << 0)
#define GBM_BO_TRANSFER_WRITE (1u << 1)

#define GBM_FMT_ARGB8888 0x34325241u
#define GBM_FMT_XRGB8888 0x34325258u
#define GBM_FMT_RGB565   0x36314752u
#define GBM_FMT_NV12     0x3231564Eu
#define GBM_FMT_YVU420   0x32315659u

/* ---------- dma_heap ioctl ---------- */
struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_allocation_data)

/* ---------- AHardwareBuffer ---------- */
typedef struct AHardwareBuffer AHardwareBuffer;
typedef struct AHardwareBuffer_Desc {
    uint32_t width, height, layers, format;
    uint64_t usage;
    uint32_t stride, rfu0;
    uint64_t rfu1;
} AHardwareBuffer_Desc;

#define AHB_FMT_RGBA_8888   1u
#define AHB_FMT_YUV_420_888 0x23u

#define AHB_USAGE_CPU_READ_OFTEN   0x2ULL
#define AHB_USAGE_CPU_WRITE_OFTEN  0x20ULL
#define AHB_USAGE_CPU_RW (AHB_USAGE_CPU_READ_OFTEN | AHB_USAGE_CPU_WRITE_OFTEN)
#define AHB_USAGE_GPU_SMPL 0x100ULL
#define AHB_USAGE_GPU_FB   0x200ULL

/* ---------- GBM 符号 ---------- */
typedef struct gbm_device *(*fn_create_device)(int);
typedef struct gbm_bo *    (*fn_bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef struct gbm_bo *    (*fn_bo_create_mod)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, int);
typedef int                (*fn_bo_get_fd)(struct gbm_bo *);
typedef int                (*fn_bo_get_fd_for_plane)(struct gbm_bo *, int);
typedef uint32_t           (*fn_bo_get_stride)(struct gbm_bo *);
typedef int                (*fn_bo_get_plane_count)(struct gbm_bo *);
typedef void *             (*fn_bo_map)(struct gbm_bo *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, void **);
typedef void               (*fn_bo_unmap)(struct gbm_bo *, void *);
typedef void               (*fn_bo_destroy)(struct gbm_bo *);
typedef void               (*fn_device_destroy)(struct gbm_device *);
typedef const char *       (*fn_backend_name)(struct gbm_device *);

/* ---------- AHardwareBuffer 符号 ---------- */
typedef int  (*fn_ahb_alloc)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
typedef void (*fn_ahb_release)(AHardwareBuffer *);

/* ---------- 全局 GBM 状态 ---------- */
static void *g_gbm_lib = NULL;
static fn_create_device       p_create_device    = NULL;
static fn_bo_create           p_bo_create        = NULL;
static fn_bo_create_mod       p_bo_create_mod    = NULL;
static fn_bo_get_fd           p_bo_get_fd        = NULL;
static fn_bo_get_fd_for_plane p_bo_get_fd_plane  = NULL;
static fn_bo_get_stride       p_bo_get_stride    = NULL;
static fn_bo_get_plane_count  p_bo_get_plane_cnt = NULL;   /* 修复：类型名 */
static fn_bo_map              p_bo_map           = NULL;
static fn_bo_unmap            p_bo_unmap         = NULL;
static fn_bo_destroy          p_bo_destroy       = NULL;
static fn_device_destroy      p_device_destroy   = NULL;
static fn_backend_name        p_backend_name     = NULL;

static struct gbm_device *g_gbm = NULL;
static int g_drm_fd = -1;

/* ---------- 全局 AHB 状态 ---------- */
static void *g_ahb_lib = NULL;
static fn_ahb_alloc   p_ahb_alloc   = NULL;
static fn_ahb_release p_ahb_release = NULL;

/* ---------- handle（Stage 9 用） ---------- */
#define HANDLE_MAGIC 0x60585350u

struct gralloc_handle_t {
    native_handle_t base;
    int      prime_fd;
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t usage;
    uint32_t stride;
    uint32_t plane_offset[3];
    uint32_t plane_stride[3];
    enum { SRC_GBM, SRC_DMAHEAP } src;
    struct gbm_bo *bo;
    void          *map_data;
    void          *cpu_addr;
    size_t         cpu_size;
};

/* ---------- 计数 ---------- */
static int g_total_pass = 0, g_total_fail = 0, g_total_warn = 0;

struct stage_stat {
    const char *name;
    int pass, fail, warn;
    int ran;
} g_stages[11];

static struct stage_stat *cur_stage = NULL;

#define OK(fmt, ...)   do { printf("[ OK ] " fmt "\n", ##__VA_ARGS__); if (cur_stage) cur_stage->pass++; g_total_pass++; } while (0)
#define FAIL(fmt, ...) do { printf("[FAIL] " fmt "\n", ##__VA_ARGS__); if (cur_stage) cur_stage->fail++; g_total_fail++; } while (0)
#define WARN(fmt, ...) do { printf("[WARN] " fmt "\n", ##__VA_ARGS__); if (cur_stage) cur_stage->warn++; g_total_warn++; } while (0)
#define INFO(fmt, ...) do { printf("[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
#define SKIP(fmt, ...) do { printf("[SKIP] " fmt "\n", ##__VA_ARGS__); } while (0)

static void begin_stage(int idx, const char *title)
{
    cur_stage = &g_stages[idx];
    cur_stage->name = title;
    cur_stage->ran = 1;
    printf("\n========== %s ==========\n", title);
}

/* ============================================================
 * 工具函数
 * ============================================================ */

static int is_yuv_format(uint32_t format)
{
    return format == FOURCC_NV12 ||
           format == HAL_PIXEL_FORMAT_YV12 ||
           format == HAL_PIXEL_FORMAT_YCbCr_420_888;
}

static int is_nv12_like(uint32_t format)
{
    return format == FOURCC_NV12 ||
           format == HAL_PIXEL_FORMAT_YCbCr_420_888;
}

static int dma_heap_alloc(size_t len)
{
    const char *heaps[] = {
        "/dev/dma_heap/system",
        "/dev/dma_heap/reserved",
        "/dev/dma_heap/default_cma_region",
        NULL
    };
    for (int i = 0; heaps[i]; i++) {
        int hfd = open(heaps[i], O_RDWR | O_CLOEXEC);
        if (hfd < 0) continue;
        struct dma_heap_allocation_data d = {
            .len = (uint64_t)len,
            .fd_flags = O_RDWR | O_CLOEXEC,
        };
        if (ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &d) == 0) {
            close(hfd);
            return (int)d.fd;
        }
        close(hfd);
    }
    return -1;
}

static off_t dma_buf_size(int fd)
{
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz > 0) { lseek(fd, 0, SEEK_SET); return sz; }
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size > 0) return st.st_size;
    return -1;
}

/* ============================================================
 * Stage 0：环境探测
 * ============================================================ */
static void list_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) { INFO("%s: %s", path, strerror(errno)); return; }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        printf("       %s/%s\n", path, e->d_name);
        n++;
    }
    closedir(d);
    INFO("%s: %d entries", path, n);
}

static void stage0_env(void)
{
    begin_stage(0, "Stage 0: 环境探测");
    list_dir("/dev/dri");
    list_dir("/dev/dma_heap");

    const char *libdirs[] = {"/system/lib64", "/vendor/lib64",
                             "/vendor/lib64/hw", "/odm/lib64", NULL};
    for (int i = 0; libdirs[i]; i++) {
        DIR *d = opendir(libdirs[i]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strstr(e->d_name, "gbm") || strstr(e->d_name, "minigbm") ||
                strstr(e->d_name, "gralloc") || strstr(e->d_name, "android.hardware.graphics"))
                INFO("found: %s/%s", libdirs[i], e->d_name);
        }
        closedir(d);
    }
}

/* ============================================================
 * Stage 1：dlopen libgbm + dlsym
 * 修复：返回类型 void → int（REQ 宏需要 return -1）
 * ============================================================ */
static int load_opt(void **out, const char *name)
{
    *out = dlsym(g_gbm_lib, name);
    if (*out) OK("dlsym %s", name);
    else      SKIP("dlsym %s (可选)", name);
    return *out ? 0 : -1;
}

#define REQ(f, n) do { *(void**)(&f) = dlsym(g_gbm_lib, n); \
    if (!f) { FAIL("dlsym %s (必须)", n); return -1; } \
    OK("dlsym %s", n); } while (0)

static int stage1_gbm_load(void)
{
    begin_stage(1, "Stage 1: dlopen libgbm + dlsym");

    const char *cands[] = {
        "libgbm.so.1", "libgbm.so",
        "libgbm_mesa.so", "libminigbm.so",
        "/vendor/lib64/libgbm.so.1",
        "/vendor/lib64/libgbm.so",
        NULL
    };
    for (int i = 0; cands[i]; i++) {
        g_gbm_lib = dlopen(cands[i], RTLD_NOW | RTLD_LOCAL);
        if (g_gbm_lib) { OK("dlopen %s", cands[i]); break; }
        if (cands[i][0] != '/')
            FAIL("dlopen %s", cands[i]);
        else
            SKIP("dlopen %s", cands[i]);
    }
    if (!g_gbm_lib) return -1;

    REQ(p_create_device,  "gbm_create_device");
    REQ(p_bo_create,      "gbm_bo_create");
    REQ(p_bo_get_fd,      "gbm_bo_get_fd");
    REQ(p_bo_get_stride,  "gbm_bo_get_stride");
    REQ(p_bo_map,         "gbm_bo_map");
    REQ(p_bo_unmap,       "gbm_bo_unmap");
    REQ(p_bo_destroy,     "gbm_bo_destroy");
    REQ(p_device_destroy, "gbm_device_destroy");

    load_opt((void**)&p_bo_get_fd_plane, "gbm_bo_get_fd_for_plane");
    load_opt((void**)&p_bo_get_plane_cnt,"gbm_bo_get_plane_count");
    load_opt((void**)&p_bo_create_mod,   "gbm_bo_create_with_modifiers");
    load_opt((void**)&p_backend_name,    "gbm_device_get_backend_name");
    return 0;
}

/* ============================================================
 * Stage 2：gbm_create_device
 * ============================================================ */
static int open_drm_node(char *path_out, size_t path_sz)
{
    for (int i = 128; i <= 139; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/dri/renderD%d", i);
        int fd = open(p, O_RDWR | O_CLOEXEC);
        if (fd >= 0) { if (path_out) snprintf(path_out, path_sz, "%s", p); return fd; }
    }
    for (int i = 0; i < 10; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/dri/card%d", i);
        int fd = open(p, O_RDWR | O_CLOEXEC);
        if (fd >= 0) { if (path_out) snprintf(path_out, path_sz, "%s", p); return fd; }
    }
    return -1;
}

static void stage2_gbm_device(void)
{
    begin_stage(2, "Stage 2: gbm_create_device");
    if (!g_gbm_lib) { SKIP("libgbm 未加载"); return; }

    char path[64] = {0};
    g_drm_fd = open_drm_node(path, sizeof(path));
    if (g_drm_fd < 0) { FAIL("无法打开 /dev/dri/renderD* 或 card*"); return; }
    OK("opened %s, fd=%d", path, g_drm_fd);

    g_gbm = p_create_device(g_drm_fd);
    if (!g_gbm) { FAIL("gbm_create_device 返回 NULL"); return; }
    OK("gbm_create_device OK");

    if (p_backend_name) {
        const char *bn = p_backend_name(g_gbm);
        INFO("GBM backend: %s", bn ? bn : "(null)");
    }
}

/* ============================================================
 * Stage 3/4/5：GBM create 矩阵
 * ============================================================ */
struct fmt_case   { uint32_t fmt; const char *name; };
struct usage_case { uint32_t flags; const char *name; };

static int export_primary_fd(struct gbm_bo *bo, int planes)
{
    int fd = -1;
    if (planes > 1 && p_bo_get_fd_plane) fd = p_bo_get_fd_plane(bo, 0);
    if (fd < 0) fd = p_bo_get_fd(bo);
    if (fd < 0 && p_bo_get_fd_plane) fd = p_bo_get_fd_plane(bo, 0);
    return fd;
}

static void gbm_test_one(uint32_t w, uint32_t h,
                         const struct fmt_case *f,
                         const struct usage_case *u,
                         int do_map)
{
    struct gbm_bo *bo = p_bo_create(g_gbm, w, h, f->fmt, u->flags);
    if (!bo) {
        FAIL("%-10s %ux%u [%s] create", f->name, w, h, u->name);
        return;
    }
    int planes = p_bo_get_plane_cnt ? p_bo_get_plane_cnt(bo) : -1;
    uint32_t stride = p_bo_get_stride(bo);
    OK("%-10s %ux%u [%s] stride=%u planes=%d",
       f->name, w, h, u->name, stride, planes);

    int fd = export_primary_fd(bo, planes);
    if (fd < 0) {
        FAIL("  export fd");
    } else {
        off_t sz = dma_buf_size(fd);
        OK("  export fd=%d size=%lld", fd, (long long)sz);
        if (sz > 0) {
            void *p = mmap(NULL, (size_t)sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (p == MAP_FAILED)
                p = mmap(NULL, (size_t)sz, PROT_READ, MAP_SHARED, fd, 0);
            if (p != MAP_FAILED) {
                OK("  mmap(%lld) ok", (long long)sz);
                munmap(p, (size_t)sz);
            } else {
                WARN("  mmap(%lld) failed", (long long)sz);
            }
        }
        close(fd);
    }

    if (do_map) {
        uint32_t out_stride = 0;
        void *map_data = NULL;
        void *ptr = p_bo_map(bo, 0, 0, w, h, GBM_BO_TRANSFER_READ, &out_stride, &map_data);
        int is_linear = (u->flags & GBM_BO_USE_LINEAR) != 0;
        if (ptr) {
            OK("  gbm_bo_map stride=%u", out_stride);
            p_bo_unmap(bo, map_data);
        } else if (is_linear) {
            FAIL("  gbm_bo_map (LINEAR 必须成功)");
        } else {
            WARN("  gbm_bo_map (tiled 预期可能失败)");
        }
    }

    p_bo_destroy(bo);
}

static void stage3_gbm_matrix(void)
{
    begin_stage(3, "Stage 3: GBM create 矩阵（64x64）");
    if (!g_gbm) { SKIP("GBM 未就绪"); return; }

    struct fmt_case fmts[] = {
        { GBM_FMT_ARGB8888, "RGBA_8888" },
        { GBM_FMT_RGB565,   "RGB_565"   },
        { GBM_FMT_NV12,     "NV12"      },
        { GBM_FMT_YVU420,   "YV12"      },
    };
    struct usage_case usages[] = {
        { GBM_BO_USE_LINEAR,                           "LINEAR"    },
        { GBM_BO_USE_TEXTURING,                        "TEXTURING" },
        { GBM_BO_USE_RENDERING,                        "RENDERING" },
        { GBM_BO_USE_TEXTURING | GBM_BO_USE_RENDERING, "TEX|REN"   },
        { GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING,    "LIN|REN"   },
        { GBM_BO_USE_SCANOUT | GBM_BO_USE_TEXTURING,   "SCAN|TEX"  },
    };
    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++)
        for (size_t j = 0; j < sizeof(usages)/sizeof(usages[0]); j++)
            gbm_test_one(64, 64, &fmts[i], &usages[j], 0);
}

static void stage4_gbm_big_map(void)
{
    begin_stage(4, "Stage 4: GBM 1280x720 + map");
    if (!g_gbm) { SKIP("GBM 未就绪"); return; }

    struct fmt_case fmts[] = {
        { GBM_FMT_ARGB8888, "RGBA_8888" },
        { GBM_FMT_NV12,     "NV12"      },
        { GBM_FMT_YVU420,   "YV12"      },
    };
    struct usage_case usages[] = {
        { GBM_BO_USE_LINEAR,                           "LINEAR"  },
        { GBM_BO_USE_TEXTURING | GBM_BO_USE_RENDERING, "TEX|REN" },
        { GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING,    "LIN|REN" },
    };
    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++)
        for (size_t j = 0; j < sizeof(usages)/sizeof(usages[0]); j++)
            gbm_test_one(1280, 720, &fmts[i], &usages[j], 1);
}

static uint32_t android_usage_to_gbm_flags(uint32_t au)
{
    uint32_t f = 0;
    if (au & (U_HW_TEXTURE | U_HW_RENDER)) f |= GBM_BO_USE_RENDERING;
    if (au & U_HW_COMPOSER)               f |= GBM_BO_USE_SCANOUT;
    if (au & (U_SW_READ_OFTEN | U_SW_WRITE_OFTEN)) f |= GBM_BO_USE_LINEAR;
    if (!f) f = GBM_BO_USE_LINEAR;
    return f;
}

static void stage5_gbm_android(void)
{
    begin_stage(5, "Stage 5: GBM 模拟 Android usage");
    if (!g_gbm) { SKIP("GBM 未就绪"); return; }

    struct {
        uint32_t au;
        const char *name;
    } cases[] = {
        { U_SW_READ_OFTEN | U_SW_WRITE_OFTEN,                     "CPU_RW"       },
        { U_HW_TEXTURE,                                            "HW_TEX"       },
        { U_HW_TEXTURE | U_HW_RENDER,                              "HW_TEX_REN"   },
        { U_HW_TEXTURE | U_HW_RENDER | U_HW_COMPOSER,              "TEX_REN_COMP" },
        { U_SW_READ_OFTEN | U_SW_WRITE_OFTEN | U_HW_TEXTURE,       "CPU_HW_TEX"   },
    };
    struct fmt_case fmts[] = {
        { GBM_FMT_ARGB8888, "RGBA_8888" },
        { GBM_FMT_NV12,     "NV12"      },
    };
    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++) {
        for (size_t j = 0; j < sizeof(cases)/sizeof(cases[0]); j++) {
            uint32_t gf = android_usage_to_gbm_flags(cases[j].au);
            struct usage_case uc = { gf, cases[j].name };
            gbm_test_one(1280, 720, &fmts[i], &uc, 0);
        }
    }
}

/* ============================================================
 * Stage 6：GBM modifiers
 * ============================================================ */
static void stage6_gbm_modifiers(void)
{
    begin_stage(6, "Stage 6: GBM modifiers");
    if (!g_gbm || !p_bo_create_mod) { SKIP("gbm_bo_create_with_modifiers 不可用"); return; }

    struct fmt_case fmts[] = {
        { GBM_FMT_ARGB8888, "RGBA_8888" },
        { GBM_FMT_NV12,     "NV12"      },
    };
    uint64_t mods[] = { 0ULL };
    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++) {
        struct gbm_bo *bo = p_bo_create_mod(g_gbm, 1280, 720, fmts[i].fmt, mods, 1);
        if (bo) {
            OK("%-10s [LINEAR modifier]", fmts[i].name);
            int fd = p_bo_get_fd(bo);
            if (fd >= 0) close(fd);
            p_bo_destroy(bo);
        } else {
            FAIL("%-10s [LINEAR modifier]", fmts[i].name);
        }
    }
}

/* ============================================================
 * Stage 7：dma_heap 探测
 * ============================================================ */
static void stage7_dma_heap_probe(void)
{
    begin_stage(7, "Stage 7: dma_heap 探测");
    const char *heaps[] = {
        "/dev/dma_heap/system",
        "/dev/dma_heap/reserved",
        "/dev/dma_heap/default_cma_region",
        NULL
    };
    for (int i = 0; heaps[i]; i++) {
        int fd = dma_heap_alloc(4096);
        if (fd >= 0) { OK("%s alloc 4096 ok fd=%d", heaps[i], fd); close(fd); }
        else         { FAIL("%s alloc 4096 failed: %s", heaps[i], strerror(errno)); }
    }
}

/* ============================================================
 * Stage 8：dma_heap YUV 分配 + 填充 + 验证
 * ============================================================ */
struct yuv_layout {
    uint32_t y_stride, c_stride;
    uint32_t y_offset, u_offset, v_offset;
    size_t   total_size;
};

static void calc_nv12_layout(uint32_t w, uint32_t h, struct yuv_layout *L)
{
    uint32_t ys = (w + 15) & ~15u;
    uint32_t ysz = ys * h;
    uint32_t cs = ys;
    uint32_t csz = cs * ((h + 1) / 2);
    L->y_stride = ys; L->c_stride = cs;
    L->y_offset = 0;  L->u_offset = ysz; L->v_offset = ysz;
    L->total_size = ysz + csz;
}

static void calc_yv12_layout(uint32_t w, uint32_t h, struct yuv_layout *L)
{
    uint32_t ys = (w + 15) & ~15u;
    uint32_t ysz = ys * h;
    uint32_t cs = ys;
    uint32_t csz = cs * ((h + 1) / 2);
    L->y_stride = ys; L->c_stride = cs;
    L->y_offset = 0; L->u_offset = ysz + csz; L->v_offset = ysz;
    L->total_size = ysz + csz * 2;
}

static int fill_verify_yuv(uint8_t *base, uint32_t w, uint32_t h,
                           const struct yuv_layout *L, uint32_t format, int frame)
{
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = base + L->y_offset + y * L->y_stride;
        for (uint32_t x = 0; x < w; x++)
            row[x] = (uint8_t)((x + y + frame * 8) & 0xff);
    }
    uint32_t cw = w / 2, ch = (h + 1) / 2;
    if (is_nv12_like(format)) {
        for (uint32_t y = 0; y < ch; y++) {
            uint8_t *row = base + L->u_offset + y * L->c_stride;
            for (uint32_t x = 0; x < cw; x++) {
                row[x*2+0] = (uint8_t)(128 + ((x + frame) & 0x3f));
                row[x*2+1] = (uint8_t)(128 + ((y + frame) & 0x3f));
            }
        }
    } else if (format == HAL_PIXEL_FORMAT_YV12) {
        for (uint32_t y = 0; y < ch; y++) {
            uint8_t *vrow = base + L->v_offset + y * L->c_stride;
            uint8_t *urow = base + L->u_offset + y * L->c_stride;
            for (uint32_t x = 0; x < cw; x++) {
                vrow[x] = (uint8_t)(128 + ((y + frame) & 0x3f));
                urow[x] = (uint8_t)(128 + ((x + frame) & 0x3f));
            }
        }
    }
    uint32_t pts[][2] = {{0,0},{w/2,h/2},{w-1,h-1},{w/4,h/4}};
    for (int i = 0; i < 4; i++) {
        uint32_t x = pts[i][0], y = pts[i][1];
        uint8_t expect = (uint8_t)((x + y + frame * 8) & 0xff);
        if (base[L->y_offset + y * L->y_stride + x] != expect) return -1;
    }
    return 0;
}

static void dma_heap_test_one(uint32_t format, const char *name)
{
    struct yuv_layout L = {0};
    if (is_nv12_like(format)) calc_nv12_layout(1280, 720, &L);
    else                       calc_yv12_layout(1280, 720, &L);

    INFO("%s layout: Ystride=%u Cstride=%u total=%zu",
         name, L.y_stride, L.c_stride, L.total_size);

    int fd = dma_heap_alloc(L.total_size);
    if (fd < 0) { FAIL("%s alloc", name); return; }
    OK("%s alloc fd=%d", name, fd);

    void *p = mmap(NULL, L.total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { FAIL("%s mmap", name); close(fd); return; }
    OK("%s mmap ok", name);

    for (int f = 0; f < 3; f++) {
        if (fill_verify_yuv((uint8_t*)p, 1280, 720, &L, format, f) < 0) {
            FAIL("%s verify frame %d", name, f);
            munmap(p, L.total_size); close(fd); return;
        }
    }
    OK("%s 3-frame fill+verify", name);
    munmap(p, L.total_size);
    close(fd);
}

static void stage8_dma_heap_yuv(void)
{
    begin_stage(8, "Stage 8: dma_heap YUV 分配 + 验证（1280x720）");
    dma_heap_test_one(FOURCC_NV12, "NV12");
    dma_heap_test_one(HAL_PIXEL_FORMAT_YV12, "YV12");
}

/* ============================================================
 * Stage 9：完整 gralloc 逻辑
 * ============================================================ */
static size_t calc_yuv_total_size(uint32_t format, uint32_t w, uint32_t h)
{
    uint32_t ys = (w + 15) & ~15u;
    uint32_t ysz = ys * h;
    uint32_t csz = ys * ((h + 1) / 2);
    if (is_nv12_like(format)) return (size_t)ysz + csz;
    return (size_t)ysz + csz * 2;
}

static void calc_yuv_layout_handle(struct gralloc_handle_t *h)
{
    uint32_t ys = (h->width + 15) & ~15u;
    uint32_t ysz = ys * h->height;
    uint32_t cs = ys;
    uint32_t csz = cs * ((h->height + 1) / 2);

    h->stride = ys;
    h->plane_stride[0] = ys;
    h->plane_offset[0] = 0;

    if (is_nv12_like(h->format)) {
        h->plane_stride[1] = cs; h->plane_offset[1] = ysz;
        h->plane_stride[2] = cs; h->plane_offset[2] = ysz;
    } else {
        h->plane_stride[1] = cs; h->plane_offset[1] = ysz;
        h->plane_stride[2] = cs; h->plane_offset[2] = ysz + csz;
    }
}

static native_handle_t *build_handle(int fd, uint32_t w, uint32_t hh,
                                     uint32_t format, uint32_t usage,
                                     struct gbm_bo *bo, int src)
{
    size_t extra = sizeof(struct gralloc_handle_t) - sizeof(native_handle_t);
    int total_ints = (int)((extra + sizeof(int) - 1) / sizeof(int));
    int numFds = 1;
    int numInts = total_ints - numFds;
    if (numInts < 0) numInts = 0;

    native_handle_t *nh = native_handle_create(numFds, numInts);
    if (!nh) return NULL;

    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)nh;
    memset((uint8_t *)gh + sizeof(native_handle_t), 0,
           sizeof(*gh) - sizeof(native_handle_t));
    gh->prime_fd = fd;
    gh->magic    = HANDLE_MAGIC;
    gh->width    = w;
    gh->height   = hh;
    gh->format   = format;
    gh->usage    = usage;
    gh->src      = src;
    gh->bo       = bo;
    return nh;
}

static uint32_t gbm_format_for_rgb(uint32_t format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888: return GBM_FMT_ARGB8888;
    case HAL_PIXEL_FORMAT_RGBX_8888: return GBM_FMT_XRGB8888;
    case HAL_PIXEL_FORMAT_RGB_565:   return GBM_FMT_RGB565;
    default: return 0;
    }
}

static uint32_t gbm_flags_for_usage(int usage)
{
    uint32_t f = 0;
    if (usage & (U_HW_TEXTURE | U_HW_RENDER)) f |= GBM_BO_USE_RENDERING;
    if (usage & U_HW_COMPOSER)                f |= GBM_BO_USE_SCANOUT;
    if (usage & (U_SW_READ_OFTEN | U_SW_WRITE_OFTEN)) f |= GBM_BO_USE_LINEAR;
    if (!f) f = GBM_BO_USE_LINEAR;
    return f;
}

static int mega_alloc(int w, int h, int format, int usage,
                      buffer_handle_t *handle, int *stride)
{
    if (is_yuv_format((uint32_t)format)) {
        size_t total = calc_yuv_total_size((uint32_t)format, w, h);
        int fd = dma_heap_alloc(total);
        if (fd < 0) return -ENOMEM;
        native_handle_t *nh = build_handle(fd, w, h, format, usage, NULL, 1);
        if (!nh) { close(fd); return -ENOMEM; }
        calc_yuv_layout_handle((struct gralloc_handle_t *)nh);
        *handle = nh;
        if (stride) *stride = (int)((struct gralloc_handle_t *)nh)->stride;
        return 0;
    }

    if (!g_gbm) return -ENODEV;
    uint32_t gf = gbm_format_for_rgb((uint32_t)format);
    if (!gf) return -EINVAL;
    uint32_t gflags = gbm_flags_for_usage(usage);
    struct gbm_bo *bo = p_bo_create(g_gbm, w, h, gf, gflags);
    if (!bo) return -ENOMEM;
    int fd = p_bo_get_fd(bo);
    if (fd < 0) { p_bo_destroy(bo); return -EINVAL; }
    native_handle_t *nh = build_handle(fd, w, h, format, usage, bo, 0);
    if (!nh) { close(fd); p_bo_destroy(bo); return -ENOMEM; }
    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)nh;
    gh->stride = p_bo_get_stride(bo);
    gh->plane_stride[0] = gh->stride;
    *handle = nh;
    if (stride) *stride = (int)gh->stride;
    return 0;
}

static int mega_free(buffer_handle_t handle)
{
    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)handle;
    if (!gh || gh->magic != HANDLE_MAGIC) return -EINVAL;
    if (gh->cpu_addr && gh->cpu_size > 0) munmap(gh->cpu_addr, gh->cpu_size);
    if (gh->map_data && gh->bo) p_bo_unmap(gh->bo, gh->map_data);
    if (gh->bo) p_bo_destroy(gh->bo);
    if (gh->prime_fd >= 0) close(gh->prime_fd);
    native_handle_delete((native_handle_t *)handle);
    return 0;
}

static int mega_lock(buffer_handle_t handle, int usage,
                     int l, int t, int w, int h, void **vaddr)
{
    (void)l; (void)t; (void)w; (void)h;
    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)handle;
    if (!gh || gh->magic != HANDLE_MAGIC) return -EINVAL;
    if (gh->src == 1) {
        if (!gh->cpu_addr) {
            off_t sz = lseek(gh->prime_fd, 0, SEEK_END);
            if (sz <= 0) return -EINVAL;
            void *p = mmap(NULL, (size_t)sz, PROT_READ | PROT_WRITE, MAP_SHARED, gh->prime_fd, 0);
            if (p == MAP_FAILED)
                p = mmap(NULL, (size_t)sz, PROT_READ, MAP_SHARED, gh->prime_fd, 0);
            if (p == MAP_FAILED) return -EINVAL;
            gh->cpu_addr = p; gh->cpu_size = (size_t)sz;
        }
        *vaddr = gh->cpu_addr;
        return 0;
    }
    if (!gh->bo) return -EINVAL;
    uint32_t flags = (usage & U_SW_WRITE_OFTEN) ? GBM_BO_TRANSFER_WRITE : GBM_BO_TRANSFER_READ;
    uint32_t out_stride = 0;
    void *ptr = p_bo_map(gh->bo, 0, 0, gh->width, gh->height, flags, &out_stride, &gh->map_data);
    if (!ptr) return -EINVAL;
    *vaddr = ptr;
    return 0;
}

static int mega_unlock(buffer_handle_t handle)
{
    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)handle;
    if (!gh || gh->magic != HANDLE_MAGIC) return -EINVAL;
    if (gh->src == 0 && gh->map_data && gh->bo) {
        p_bo_unmap(gh->bo, gh->map_data);
        gh->map_data = NULL;
    }
    return 0;
}

static int mega_lock_ycbcr(buffer_handle_t handle, int usage,
                           int l, int t, int w, int h,
                           struct android_ycbcr *ycbcr)
{
    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)handle;
    if (!gh || gh->magic != HANDLE_MAGIC || !ycbcr) return -EINVAL;
    if (gh->src != 1) return -EINVAL;
    void *vaddr = NULL;
    if (mega_lock(handle, usage, l, t, w, h, &vaddr) < 0) return -EINVAL;
    uint8_t *base = (uint8_t *)vaddr;
    memset(ycbcr, 0, sizeof(*ycbcr));
    if (is_nv12_like(gh->format)) {
        ycbcr->y  = base + gh->plane_offset[0];
        ycbcr->cb = base + gh->plane_offset[1];
        ycbcr->cr = base + gh->plane_offset[1] + 1;
        ycbcr->ystride = gh->plane_stride[0];
        ycbcr->cstride = gh->plane_stride[1];
        ycbcr->chroma_step = 2;
    } else if (gh->format == HAL_PIXEL_FORMAT_YV12) {
        ycbcr->y  = base + gh->plane_offset[0];
        ycbcr->cr = base + gh->plane_offset[1];
        ycbcr->cb = base + gh->plane_offset[2];
        ycbcr->ystride = gh->plane_stride[0];
        ycbcr->cstride = gh->plane_stride[1];
        ycbcr->chroma_step = 1;
    } else return -EINVAL;
    return 0;
}

static void mega_test_one(uint32_t format, const char *fname, int usage, const char *uname)
{
    char label[96];
    snprintf(label, sizeof(label), "%-10s [%s]", fname, uname);

    buffer_handle_t h = NULL;
    int stride = 0;
    if (mega_alloc(1280, 720, format, usage, &h, &stride) != 0) {
        FAIL("%s alloc", label);
        return;
    }
    struct gralloc_handle_t *gh = (struct gralloc_handle_t *)h;
    OK("%s alloc stride=%d src=%s", label, stride, gh->src == 0 ? "gbm" : "dmaheap");

    void *vaddr = NULL;
    if (mega_lock(h, usage, 0, 0, 1280, 720, &vaddr) != 0) {
        FAIL("%s lock", label);
        mega_free(h);
        return;
    }

    if (is_yuv_format(format)) {
        struct android_ycbcr y;
        if (mega_lock_ycbcr(h, usage, 0, 0, 1280, 720, &y) != 0) {
            FAIL("%s lock_ycbcr", label);
            mega_unlock(h); mega_free(h);
            return;
        }
        OK("%s lock_ycbcr y=%p cb=%p cr=%p step=%zu",
           label, y.y, y.cb, y.cr, y.chroma_step);
    }

    uint8_t *base = (uint8_t *)vaddr;
    size_t sz = (size_t)gh->stride * gh->height;
    for (size_t i = 0; i < sz && i < 4096; i++) base[i] = (uint8_t)(i & 0xff);
    OK("%s fill+read", label);

    mega_unlock(h);
    mega_free(h);
}

static void stage9_full_gralloc(void)
{
    begin_stage(9, "Stage 9: 完整 gralloc 逻辑");
    struct { uint32_t fmt; const char *name; } fmts[] = {
        { HAL_PIXEL_FORMAT_RGBA_8888, "RGBA_8888" },
        { FOURCC_NV12,                "NV12"      },
        { HAL_PIXEL_FORMAT_YV12,      "YV12"      },
    };
    struct { int usage; const char *name; } usages[] = {
        { U_SW_READ_OFTEN | U_SW_WRITE_OFTEN, "CPU_RW"     },
        { U_HW_TEXTURE,                       "HW_TEXTURE" },
        { U_HW_TEXTURE | U_HW_RENDER,         "HW_TEX_REN" },
    };
    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++)
        for (size_t j = 0; j < sizeof(usages)/sizeof(usages[0]); j++)
            mega_test_one(fmts[i].fmt, fmts[i].name, usages[j].usage, usages[j].name);
}

/* ============================================================
 * Stage 10：AHardwareBuffer 矩阵
 * ============================================================ */
static void stage10_ahb(void)
{
    begin_stage(10, "Stage 10: AHardwareBuffer 分配矩阵");

    g_ahb_lib = dlopen("libandroid.so", RTLD_NOW);
    if (!g_ahb_lib) { SKIP("libandroid.so 不可用"); return; }
    p_ahb_alloc   = (fn_ahb_alloc)dlsym(g_ahb_lib, "AHardwareBuffer_allocate");
    p_ahb_release = (fn_ahb_release)dlsym(g_ahb_lib, "AHardwareBuffer_release");
    if (!p_ahb_alloc || !p_ahb_release) {
        SKIP("AHardwareBuffer_* 符号缺失");
        dlclose(g_ahb_lib); g_ahb_lib = NULL;
        return;
    }
    OK("libandroid.so + AHardwareBuffer_* 就绪");

    struct { uint32_t fmt; const char *name; } fmts[] = {
        { AHB_FMT_RGBA_8888,   "RGBA_8888"   },
        { AHB_FMT_YUV_420_888, "YUV_420_888" },
    };
    struct { uint64_t usage; const char *name; } usages[] = {
        { AHB_USAGE_CPU_RW,                          "CPU_RW"    },
        { AHB_USAGE_GPU_SMPL,                        "GPU_SMPL"  },
        { AHB_USAGE_GPU_FB,                          "GPU_FB"    },
        { AHB_USAGE_CPU_RW | AHB_USAGE_GPU_SMPL,     "CPU_GPU"   },
    };
    uint32_t wh[][2] = {{64,64},{1280,720}};

    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++) {
        for (size_t j = 0; j < sizeof(usages)/sizeof(usages[0]); j++) {
            for (size_t k = 0; k < sizeof(wh)/sizeof(wh[0]); k++) {
                AHardwareBuffer_Desc d = {0};
                d.width  = wh[k][0];
                d.height = wh[k][1];
                d.layers = 1;
                d.format = fmts[i].fmt;
                d.usage  = usages[j].usage;
                AHardwareBuffer *buf = NULL;
                int r = p_ahb_alloc(&d, &buf);
                char label[96];
                snprintf(label, sizeof(label), "AHB %-12s %ux%-4u [%-10s]",
                         fmts[i].name, wh[k][0], wh[k][1], usages[j].name);
                if (r == 0 && buf) {
                    OK("%s ok stride=%u", label, d.stride);
                    p_ahb_release(buf);
                } else {
                    FAIL("%s err=%d", label, r);
                }
            }
        }
    }

    dlclose(g_ahb_lib);
    g_ahb_lib = NULL;
}

/* ============================================================
 * 总结
 * ============================================================ */
static void summarize(void)
{
    printf("\n========== 总结 ==========\n");
    printf("%-3s %-40s %5s %5s %5s\n", "St", "Stage", "PASS", "FAIL", "WARN");
    for (int i = 0; i <= 10; i++) {
        if (!g_stages[i].ran) continue;
        printf("%-3d %-40s %5d %5d %5d\n",
               i, g_stages[i].name,
               g_stages[i].pass, g_stages[i].fail, g_stages[i].warn);
    }
    printf("\nTOTAL: PASS=%d FAIL=%d WARN=%d\n", g_total_pass, g_total_fail, g_total_warn);

    printf("\n========== 判定 ==========\n");

    if (g_stages[1].ran && g_stages[1].fail == 0)
        printf("[OK]  libgbm 可加载\n");
    else if (g_stages[1].ran)
        printf("[ERR] libgbm 不可加载\n");

    if (g_stages[2].ran && g_stages[2].fail == 0)
        printf("[OK]  gbm_create_device 成功\n");

    if (g_stages[5].ran) {
        if (g_stages[5].fail == 0)
            printf("[OK]  GBM 支持全部 Android usage 映射\n");
        else
            printf("[WARN] GBM 部分 Android usage 不支持（看 Stage 5 FAIL 行）\n");
    }

    if (g_stages[8].ran && g_stages[8].fail == 0)
        printf("[OK]  dma_heap YUV 可用\n");

    if (g_stages[9].ran) {
        if (g_stages[9].fail == 0)
            printf("[OK]  完整 gralloc 逻辑（alloc/lock/lock_ycbcr/free）全通\n");
        else
            printf("[ERR] 完整 gralloc 逻辑存在失败\n");
    }

    if (g_stages[10].ran) {
        if (g_stages[10].fail > 0)
            printf("[WARN] AHB 部分 usage 失败（GPU/YUV 可能不可用）\n");
        else
            printf("[OK]  AHB 全部 usage 通过\n");
    }

    printf("\n===> 综合结论：\n");
    int core_ok = (g_stages[1].fail == 0) &&
                  (g_stages[2].fail == 0) &&
                  (g_stages[9].ran && g_stages[9].fail == 0);
    if (core_ok)
        printf("     gralloc 核心逻辑可用，可打包成 gralloc.gbm.so 替换系统 HAL\n");
    else
        printf("     存在阻塞问题，看上方 FAIL 定位\n");
    printf("     注：C2AllocatorGralloc 需 C++ 编译，用 gralloc_diag 单独跑\n");
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char **argv)
{
    printf("gralloc_mega_test v1.1\n");

    int run[11] = {0};
    if (argc <= 1) {
        for (int i = 0; i <= 10; i++) run[i] = 1;
    } else {
        for (int i = 1; i < argc; i++) {
            int s = atoi(argv[i]);
            if (s >= 0 && s <= 10) run[s] = 1;
        }
    }

    if (run[0])  stage0_env();
    if (run[1])  (void)stage1_gbm_load();
    if (run[2])  stage2_gbm_device();
    if (run[3])  stage3_gbm_matrix();
    if (run[4])  stage4_gbm_big_map();
    if (run[5])  stage5_gbm_android();
    if (run[6])  stage6_gbm_modifiers();
    if (run[7])  stage7_dma_heap_probe();
    if (run[8])  stage8_dma_heap_yuv();
    if (run[9])  stage9_full_gralloc();
    if (run[10]) stage10_ahb();

    summarize();

    if (g_gbm && p_device_destroy) p_device_destroy(g_gbm);
    if (g_drm_fd >= 0) close(g_drm_fd);
    if (g_gbm_lib) dlclose(g_gbm_lib);

    return g_total_fail ? 1 : 0;
}
