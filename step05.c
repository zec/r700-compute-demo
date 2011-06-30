/**
 * step05.c
 *
 * Copyright © 2011 Zachary Catlin <z@zc.is>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S), COPYRIGHT HOLDER(S), AND/OR THEIR SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <inttypes.h>
#include <stdio.h>

#include <sys/ioctl.h>

#include <xf86drm.h>
#include <radeon_drm.h>
#include <radeon_bo.h>
#include <radeon_bo_gem.h>
#include <radeon_cs.h>
#include <radeon_cs_gem.h>

#include "r600_reg.h"

#define BUF_SIZE 256

#define HLINE(s) "---------------- " s " ----------------\n"

/* Low n bits mask */
#define LOB(n) ((1 << (n)) - 1)

/*
 * Information about the card I'm targeting (RV730 series),
 * from src/r6xx_accel.c in xf86-video-ati
 */
#define sq_ps_prio 0
#define sq_vs_prio 1
#define sq_gs_prio 2
#define sq_es_prio 3
#define sq_num_ps_gprs 84
#define sq_num_vs_gprs 36
#define sq_num_temp_gprs 4
#define sq_num_gs_gprs 0
#define sq_num_es_gprs 0
#define sq_num_ps_threads 188
#define sq_num_vs_threads 60
#define sq_num_gs_threads 0
#define sq_num_es_threads 0
#define sq_num_ps_stack_entries 128
#define sq_num_vs_stack_entries 128
#define sq_num_gs_stack_entries 0
#define sq_num_es_stack_entries 0

static unsigned char x[BUF_SIZE];

static void initialize_x()
{
    size_t i;

    for(i = 0; i < BUF_SIZE; i++)
        x[i] = (i * 50) % 253;
}

static void print_bo_info(struct radeon_bo *bo)
{
    fprintf(stderr, "  mapped to %p\n  flags: %" PRIx32
            "\n  handle: %" PRIx32 "\n  size: %" PRIx32 "\n",
            bo->ptr, bo->flags, bo->handle, bo->size);
}

/* This assumes that n is a multiple of 8 */
static void print_buffers(const unsigned char *orig,
                          const unsigned char *buf, size_t n)
{
    size_t i;

    fputs("        original        |          buffer\n", stderr);
#define B "%02x "
    for(i = 0; i < n / 8; i++)
        fprintf(stderr, B B B B B B B B "  " B B B B B B B B "\n",
                orig[i*8], orig[i*8+1], orig[i*8+2], orig[i*8+3],
                orig[i*8+4], orig[i*8+5], orig[i*8+6], orig[i*8+7],
                buf[i*8], buf[i*8+1], buf[i*8+2], buf[i*8+3],
                buf[i*8+4], buf[i*8+5], buf[i*8+6], buf[i*8+7]);
#undef B
}

static void flush_indirect(struct radeon_cs *cs)
{
    radeon_cs_emit(cs);
    radeon_cs_erase(cs);
}

/* Command-stream size check */
static inline void check_and_begin(struct radeon_cs *cs, size_t num_dw,
                                   const char *file, const char *func, int line)
{
    if(cs->cdw + num_dw > cs->ndw)
        flush_indirect(cs);

    radeon_cs_begin(cs, num_dw, file, func, line);
}
#define CAB(cs, ndw) check_and_begin((cs), (ndw), __FILE__, __func__, __LINE__)

#define END(cs) radeon_cs_end((cs), __FILE__, __func__, __LINE__)

/* Convenience abbreviation */
#define rad_cwd radeon_cs_write_dword

/* Begin a type-0 packet */
static inline void begin_packet0(struct radeon_cs *cs, uint32_t base_index,
                                 uint32_t data_count, const char *file,
                                 const char *func, int line)
{
    check_and_begin(cs, data_count + 1, file, func, line);
    rad_cwd(cs, ( ((data_count - 1) & LOB(14)) << 16 ) |
                (base_index & LOB(16)));
}
#define PACK0(cs, idx, cdw) begin_packet0((cs), (idx), (cdw), __FILE__, __func__, __LINE__)

/* Begin a type-3 packet */
static inline void begin_packet3(struct radeon_cs *cs, uint32_t opcode,
                                 uint32_t data_count, const char *file,
                                 const char *func, int line)
{
    check_and_begin(cs, data_count + 1, file, func, line);
    rad_cwd(cs, 0xc0000000 |
                ( ((data_count - 1) & LOB(14)) << 16 ) |
                ( (opcode & LOB(8)) << 8 ));
}
#define PACK3(cs, op, cdw) begin_packet3((cs), (op), (cdw), __FILE__, __func__, __LINE__)

/* Begin a type-3 packet, without the check */
static inline void unchecked_packet3(struct radeon_cs *cs, uint32_t opcode,
                                     uint32_t data_count)
{
    rad_cwd(cs, 0xc0000000 |
                ( ((data_count - 1) & LOB(14)) << 16 ) |
                ( (opcode & LOB(8)) << 8 ));
}

/* Converts a register address to a type-3 packet register offset */
static inline uint32_t to_cmd_idx(uint32_t reg)
{
    if(reg < SET_CONFIG_REG_end)
        return (reg - SET_CONFIG_REG_offset) >> 2;
    else if(reg < SET_CONTEXT_REG_end)
        return (reg - SET_CONTEXT_REG_offset) >> 2;
    else if(reg < SET_ALU_CONST_end)
        return (reg - SET_ALU_CONST_offset) >> 2;
    else if(reg < SET_RESOURCE_end)
        return (reg - SET_RESOURCE_offset) >> 2;
    else if(reg < SET_SAMPLER_end)
        return (reg - SET_SAMPLER_offset) >> 2;
    else if(reg < SET_CTL_CONST_end)
        return (reg - SET_CTL_CONST_offset) >> 2;
    else if(reg < SET_LOOP_CONST_end)
        return (reg - SET_LOOP_CONST_offset) >> 2;
    else if(reg < SET_BOOL_CONST_end)
        return (reg - SET_BOOL_CONST_offset) >> 2;
    else
        return 0;
}

/* A convenience wrapper for a common operation: setting a single register */
#define REG_PACK3(cs, op, reg, val) \
do { \
    PACK3((cs), (op), 2); \
    rad_cwd((cs), to_cmd_idx((reg))); \
    rad_cwd((cs), (val)); \
    END((cs)); \
} while(0)

/* A convenience wrapper for setting multiple registers with constant values */
#define MULTI_REG(cs, op, reg, ...) \
do { \
    static const uint32_t reg_vals[] = {__VA_ARGS__}; \
    size_t k = 0; \
    PACK3( (cs), (op), (sizeof(reg_vals) / sizeof(uint32_t)) + 1 ); \
    rad_cwd( (cs), to_cmd_idx((reg)) ); \
    while(k < sizeof(reg_vals) / sizeof(uint32_t)) \
        rad_cwd((cs), reg_vals[k++]); \
    END((cs)); \
} while(0)

/* Take the value of a numeric token, and turn it into a string */
#define ZSTR1(x) #x
#define ZSTR(x) ZSTR1(x)

#define RELOC(cs, bo, rd, wd) \
do { \
    if(radeon_cs_write_reloc((cs), (bo), (rd), (wd), 0) != 0) { \
        fputs(__FILE__ ":" ZSTR(__LINE__) ": Buffer relocation failed\n", stderr); \
        rval = 1; \
        goto cleanup; \
    } \
} while(0)

/* A convenience wrapper for a single-register set and a relocation */
#define REG_RELOC(cs, op, reg, val, bo, rd, wd) \
do { \
    check_and_begin((cs), 5, __FILE__, __func__, __LINE__); \
    unchecked_packet3((cs), (op), 2); \
    rad_cwd((cs), to_cmd_idx((reg))); \
    rad_cwd((cs), (val)); \
    RELOC(cs, bo, rd, wd); /* writes two dwords */ \
    END((cs)); \
} while(0)

int main(int argc, char **argv)
{
    int drm_fd = -1, rval = 0;
    drmSetVersion sv;
    struct radeon_bo_manager *bufmgr = NULL;
    int bo_mapped = 0, bo2_mapped = 0;
    struct radeon_bo *bo = NULL, *bo2 = NULL, *shader = NULL;
    struct drm_radeon_gem_info meminfo;
    size_t i;
    unsigned char *ptr = NULL;
    struct radeon_cs_manager *cmdmgr = NULL;
    struct radeon_cs *cs = NULL;
    uint32_t *sptr = NULL;

    fputs("Hello world!\n", stderr);

    /* Using the driver for & bus address of *my* particular card for now... */
    if((drm_fd = drmOpen("radeon", "PCI:2:0:0")) < 0) {
        fprintf(stderr, "Could not open DRM device (return value %d)\n", drm_fd);
        rval = 1;
        goto cleanup;
    }

    fprintf(stderr, "Got fd %d\n", drm_fd);

    /*
     * Using the same parameters as in radeondemo
     * (git://anongit.freedesktop.org/~airlied/radeondemo)...
     */
    sv.drm_di_major = 1;
    sv.drm_di_minor = 1;
    sv.drm_dd_major = -1;
    sv.drm_dd_minor = -1;

    if(drmSetInterfaceVersion(drm_fd, &sv) < 0) {
        perror("Failed to set DRM interface version to 1.1");
        rval = 1;
        goto cleanup;
    }

#if 0
    if(ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0) < 0) {
        perror("Failed to gain master status");
        rval = 1;
        goto cleanup;
    }
#endif

    if(!drmCommandWriteRead(drm_fd, DRM_RADEON_GEM_INFO, &meminfo, sizeof(meminfo)))
        fprintf(stderr,
                "GART size: %8" PRIx64 "\nVRAM size: %8" PRIx64
                "\n VRAM vis: %8" PRIx64 "\n",
                meminfo.gart_size, meminfo.vram_size, meminfo.vram_visible);
    else {
        fputs("Ioctl DRM_RADEON_GEM_INFO failed\n", stderr);
        meminfo.gart_size = meminfo.vram_visible = UINT64_C(0);
    }

    /* Going to assume proper kernel support is there from here on... */

    if((bufmgr = radeon_bo_manager_gem_ctor(drm_fd)) == NULL) {
        fputs("Could not initialize a buffer object manager\n", stderr);
        rval = 1;
        goto cleanup;
    }

    /* Make buffer object */
    if((bo = radeon_bo_open(bufmgr,   /* buffer manager */
                            0,        /* handle (0 for new) */
                            BUF_SIZE, /* size (in bytes) */
                            4096,     /* alignment (in bytes) */
                            RADEON_GEM_DOMAIN_VRAM, /* memory domain */
                            0))       /* flags */
       == NULL) {
        fputs("Could not create the desired buffer object\n", stderr);
        rval = 1;
        goto cleanup;
    }

    initialize_x();

    /* Make a writable mapping of the buffer object into system memory */
    if((radeon_bo_map(bo, 1) != 0) || (bo->ptr == NULL)) {
        fputs("Could not map buffer object into main memory\n", stderr);
        rval = 1;
        goto cleanup;
    } else {
        bo_mapped = 1;
        fputs("Buffer object mapped\n", stderr);
        print_bo_info(bo);
        ptr = bo->ptr;
    }

    for(i = 0; i < BUF_SIZE; i++)
        ptr[i] = x[i];

    /* Unmap buffer object */
    radeon_bo_unmap(bo);
    bo_mapped = 0;
    fputs("Buffer object unmapped\n", stderr);
    print_bo_info(bo);

    /* Map the buffer back into system memory, this time read-only */
    if((radeon_bo_map(bo, 0) != 0) || (bo->ptr == NULL)) {
        fputs("Could not map buffer object a second time\n", stderr);
        rval = 1;
        goto cleanup;
    } else {
        bo_mapped = 1;
        fputs("Mapped buffer object again\n", stderr);
        print_bo_info(bo);
        ptr = bo->ptr;
    }

    fputs(HLINE("BUFFER 1"), stderr);
    print_buffers(x, ptr, BUF_SIZE);

    radeon_bo_unmap(bo);
    bo_mapped = 0;

    if((bo2 = radeon_bo_open(bufmgr, 0, BUF_SIZE, 4096, RADEON_GEM_DOMAIN_VRAM, 0))
       == NULL) {
        fputs("Could not create a second buffer object\n", stderr);
        rval = 1;
        goto cleanup;
    }

    if((shader = radeon_bo_open(bufmgr, 0, 4096, 4096, RADEON_GEM_DOMAIN_VRAM, 0)) == NULL) {
        fputs("Could not create shader\'s buffer object\n", stderr);
        rval = 1;
        goto cleanup;
    }

    if((radeon_bo_map(shader, 1) != 0) || (shader->ptr == NULL)) {
        fputs("Could not map shader\'s buffer object\n", stderr);
        rval = 1;
        goto cleanup;
    } else {
        fputs("Shader buffer object mapped\n", stderr);
        print_bo_info(shader);
        sptr = shader->ptr;
    }

    /* Shader program */
    *sptr++ = 0x00000000;

    radeon_bo_unmap(shader);

    if((cmdmgr = radeon_cs_manager_gem_ctor(drm_fd)) == NULL) {
        fputs("Could not create a command stream manager\n", stderr);
        rval = 1;
        goto cleanup;
    }

    if((cs = radeon_cs_create(cmdmgr, 16384)) == NULL) { /* 16384 dword command-stream buffer */
        fputs("Could not create a command stream\n", stderr);
        rval = 1;
        goto cleanup;
    }

    radeon_cs_set_limit(cs, RADEON_GEM_DOMAIN_VRAM, meminfo.vram_visible);
    radeon_cs_set_limit(cs, RADEON_GEM_DOMAIN_GTT, meminfo.gart_size);

    radeon_cs_space_set_flush(cs, (void (*)(void *)) flush_indirect, cs);

    /* Start 3D engine */
    PACK3(cs, IT_CONTEXT_CONTROL, 2);
    rad_cwd(cs, 0x80000000);
    rad_cwd(cs, 0x80000000);
    END(cs);

    /* Set SX_MISC */
    REG_PACK3(cs, IT_SET_CONTEXT_REG, SX_MISC, 0);

    /* Set SX_ALPHA_TEST_CONTROL and CB_BLEND_{RED,GREEN,BLUE,ALPHA} */
    MULTI_REG(cs, IT_SET_CONTEXT_REG, SX_ALPHA_TEST_CONTROL,
              0, 0, 0, 0, 0);

    /* Set SX_MEMORY_EXPORT_SIZE */
    REG_PACK3(cs, IT_SET_CONFIG_REG, SX_MEMORY_EXPORT_SIZE, 0);

    /* Set depth-buffer registers */
    REG_PACK3(cs, IT_SET_CONTEXT_REG, DB_DEPTH_CONTROL, 0);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, DB_SHADER_CONTROL, 0);
    MULTI_REG(cs, IT_SET_CONTEXT_REG, DB_RENDER_CONTROL,
              STENCIL_COMPRESS_DISABLE_bit | DEPTH_COMPRESS_DISABLE_bit, /* TODO: look into COLOR_DISABLE_bit (may be R800-specific) */
              0); /* DB_RENDER_OVERRIDE */
    REG_PACK3(cs, IT_SET_CONTEXT_REG, DB_DEPTH_VIEW, 0);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, DB_PRELOAD_CONTROL, 0);
    MULTI_REG(cs, IT_SET_CONTEXT_REG, DB_SRESULTS_COMPARE_STATE0, 0,
              0); /* DB_SRESULTS_COMPARE_STATE1 */
    MULTI_REG(cs, IT_SET_CONTEXT_REG, DB_STENCIL_CLEAR, 0,
              0 /* DB_DEPTH_CLEAR */);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, DB_ALPHA_TO_MASK, 0);

    /* Shader control */
    REG_PACK3(cs, IT_SET_CONFIG_REG, R7xx_SQ_DYN_GPR_CNTL_PS_FLUSH_REQ, 0);
    MULTI_REG(cs, IT_SET_CONFIG_REG, SQ_CONFIG,
              VC_ENABLE_bit | /* SQ_CONFIG */
              EXPORT_SRC_C_bit |
              ALU_INST_PREFER_VECTOR_bit |
              (sq_ps_prio << PS_PRIO_shift) |
              (sq_vs_prio << VS_PRIO_shift) |
              (sq_gs_prio << GS_PRIO_shift) |
              (sq_es_prio << ES_PRIO_shift),
              (sq_num_ps_gprs << NUM_PS_GPRS_shift) | /* SQ_GPR_RESOURCE_MGMT_1 */
              (sq_num_vs_gprs << NUM_VS_GPRS_shift) |
              (sq_num_temp_gprs << NUM_CLAUSE_TEMP_GPRS_shift),
              (sq_num_gs_gprs << NUM_GS_GPRS_shift) | /* SQ_GPR_RESOURCE_MGMT_2 */
              (sq_num_es_gprs << NUM_ES_GPRS_shift),
              (sq_num_ps_threads << NUM_PS_THREADS_shift) | /* SQ_THREAD_RESOURCE_MGMT */
              (sq_num_vs_threads << NUM_VS_THREADS_shift) |
              (sq_num_gs_threads << NUM_GS_THREADS_shift) |
              (sq_num_es_threads << NUM_ES_THREADS_shift),
              (sq_num_ps_stack_entries << NUM_PS_STACK_ENTRIES_shift) | /* SQ_STACK_RESOURCE_MGMT_1 */
              (sq_num_vs_stack_entries << NUM_VS_STACK_ENTRIES_shift),
              (sq_num_gs_stack_entries << NUM_GS_STACK_ENTRIES_shift) | /* SQ_STACK_RESOURCE_MGMT_2 */
              (sq_num_es_stack_entries << NUM_ES_STACK_ENTRIES_shift));

    MULTI_REG(cs, IT_SET_CONTEXT_REG, SQ_ESGS_RING_ITEMSIZE,
              0, /* SQ_ESGS_RING_ITEMSIZE */
              0, /* SQ_GSVS_RING_ITEMSIZE */
              0, /* SQ_ESTMP_RING_ITEMSIZE */
              0, /* SQ_GSTMP_RING_ITEMSIZE */
              0, /* SQ_VSTMP_RING_ITEMSIZE */
              0, /* SQ_PSTMP_RING_ITEMSIZE */
              0, /* SQ_FBUF_RING_ITEMSIZE */
              0, /* SQ_REDUC_RING_ITEMSIZE */
              0); /* SQ_GS_VERT_ITEMSIZE */

    MULTI_REG(cs, IT_SET_CTL_CONST, SQ_VTX_BASE_VTX_LOC,
              0, /* SQ_VTX_BASE_VTX_LOC */
              0); /* SQ_VTX_START_INST_LOC */

    MULTI_REG(cs, IT_SET_CONTEXT_REG, SQ_VTX_SEMANTIC_0,
              0, 0, 0, 0, 0, 0, 0, 0, /* SQ_VTX_SEMANTIC_[0-7] */
              0, 0, 0, 0, 0, 0, 0, 0, /* 8-15 */
              0, 0, 0, 0, 0, 0, 0, 0, /* 16-23 */
              0, 0, 0, 0, 0, 0, 0, 0); /* 24-31 */

    REG_PACK3(cs, IT_SET_CONTEXT_REG, SQ_PGM_RESOURCES_PS, 0);

    MULTI_REG(cs, IT_SET_CONTEXT_REG, CB_BLEND0_CONTROL,
              0, 0, 0, 0, 0, 0, 0, 0); /* CB_BLEND[0-7]_CONTROL */

    REG_PACK3(cs, IT_SET_CONTEXT_REG, SQ_PGM_RESOURCES_FS, 0);

    /* VGT registers */
    MULTI_REG(cs, IT_SET_CONTEXT_REG, VGT_OUTPUT_PATH_CNTL,
              0, /* VGT_OUTPUT_PATH_CNTL */
              0, /* VGT_HOS_CNTL */
              0, /* VGT_HOS_MAX_TESS_LEVEL */
              0, /* VGT_HOS_MIN_TESS_LEVEL */
              0, /* VGT_HOS_REUSE_DEPTH */
              0, /* VGT_GROUP_PRIM_TYPE */
              0, /* VGT_GROUP_FIRST_DECR */
              0, /* VGT_GROUP_DECR */
              0, /* VGT_GROUP_VECT_0_CNTL */
              0, /* VGT_GROUP_VECT_1_CNTL */
              0, /* VGT_GROUP_VECT_0_FMT_CNTL */
              0, /* VGT_GROUP_VECT_1_FMT_CNTL */
              0); /* VGT_GS_MODE */

    MULTI_REG(cs, IT_SET_CONTEXT_REG, VGT_STRMOUT_EN,
              0, /* VGT_STRMOUT_EN */
              0, /* VGT_REUSE_OFF */
              0); /* VGT_VTX_CNT_EN */

    REG_PACK3(cs, IT_SET_CONTEXT_REG, VGT_STRMOUT_BUFFER_EN, 0);

    /* primitive assembly registers */
    REG_PACK3(cs, IT_SET_CONFIG_REG, PA_CL_ENHANCE,
              CLIP_VTX_REORDER_ENA_bit | (3 << NUM_CLIP_SEQ_shift));
    REG_PACK3(cs, IT_SET_CONFIG_REG, PA_CL_CLIP_CNTL, 0);

    MULTI_REG(cs, IT_SET_CONTEXT_REG, PA_SC_VPORT_ZMIN_0,
              0, /* PA_SC_VPORT_ZMIN_0 = 0.0f */
              0x3f800000); /* PA_SC_VPORT_ZMAX_0 = 1.0f */

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SC_WINDOW_OFFSET, 0);

    MULTI_REG(cs, IT_SET_CONTEXT_REG, PA_SC_CLIPRECT_RULE,
              CLIP_RULE_mask, /* PA_SC_CLIPRECT_RULE */
              (0 << PA_SC_CLIPRECT_0_TL__TL_X_shift) | /* PA_SC_CLIPRULE_0_TL */
              (0 << PA_SC_CLIPRECT_0_TL__TL_Y_shift),
              (8192 << PA_SC_CLIPRECT_0_BR__BR_X_shift) | /* PA_SC_CLIPRULE_0_BR */
              (8192 << PA_SC_CLIPRECT_0_BR__BR_Y_shift),
              (0 << PA_SC_CLIPRECT_0_TL__TL_X_shift) | /* PA_SC_CLIPRULE_1_TL */
              (0 << PA_SC_CLIPRECT_0_TL__TL_Y_shift),
              (8192 << PA_SC_CLIPRECT_0_BR__BR_X_shift) | /* PA_SC_CLIPRULE_1_BR */
              (8192 << PA_SC_CLIPRECT_0_BR__BR_Y_shift),
              (0 << PA_SC_CLIPRECT_0_TL__TL_X_shift) | /* PA_SC_CLIPRULE_2_TL */
              (0 << PA_SC_CLIPRECT_0_TL__TL_Y_shift),
              (8192 << PA_SC_CLIPRECT_0_BR__BR_X_shift) | /* PA_SC_CLIPRULE_2_BR */
              (8192 << PA_SC_CLIPRECT_0_BR__BR_Y_shift),
              (0 << PA_SC_CLIPRECT_0_TL__TL_X_shift) | /* PA_SC_CLIPRULE_3_TL */
              (0 << PA_SC_CLIPRECT_0_TL__TL_Y_shift),
              (8192 << PA_SC_CLIPRECT_0_BR__BR_X_shift) | /* PA_SC_CLIPRULE_3_BR */
              (8192 << PA_SC_CLIPRECT_0_BR__BR_Y_shift));

    for(i = 0; i < PA_SC_VPORT_SCISSOR_0_TL_num; i++)
      MULTI_REG(cs, IT_SET_CONTEXT_REG,
                PA_SC_VPORT_SCISSOR_0_TL + i * PA_SC_VPORT_SCISSOR_0_TL_offset,
                (0 << PA_SC_VPORT_SCISSOR_0_TL__TL_X_shift) | /* PA_SC_VPORT_SCISSOR_[i]_TL */
                (0 << PA_SC_VPORT_SCISSOR_0_TL__TL_Y_shift),
                (8192 << PA_SC_VPORT_SCISSOR_0_BR__BR_X_shift) | /* PA_SC_VPORT_SCISSOR_[i]_BR */
                (8192 << PA_SC_VPORT_SCISSOR_0_BR__BR_Y_shift));

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SC_MODE_CNTL, 0);

    MULTI_REG(cs, IT_SET_CONTEXT_REG, PA_SC_LINE_CNTL,
              0, /* PA_SC_LINE_CNTL */
              0, /* PA_SC_AA_CONFIG */
              (X_ROUND_TO_EVEN << PA_SU_VTX_CNTL__ROUND_MODE_shift) | /* PA_SU_VTX_CNTL */
              PIX_CENTER_bit,
              0x3f800000, /* PA_CL_GB_VERT_CLIP_ADJ = 1.0f */
              0x3f800000, /* PA_CL_GB_VERT_DISC_ADJ = 1.0f */
              0x3f800000, /* PA_CL_GB_HORZ_CLIP_ADJ = 1.0f */
              0x3f800000, /* PA_CL_GB_HORZ_DISC_ADJ = 1.0f */
              0, /* PA_SC_AA_SAMPLE_LOCS_MCTX */
              0); /* PA_SC_AA_SAMPLE_LOCS_8S_WD1_MCTX */

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SC_AA_MASK, 0xFFFFFFFF);

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_CL_CLIP_CNTL, CLIP_DISABLE_bit);
    MULTI_REG(cs, IT_SET_CONTEXT_REG, PA_CL_VTE_CNTL,
              VTX_XY_FMT_bit, /* PA_CL_VTE_CNTL */
              0, /* PA_CL_VS_OUT_CNTL */
              0); /* PA_CL_NANINF_CNTL */

    MULTI_REG(cs, IT_SET_CONTEXT_REG, PA_SU_POLY_OFFSET_DB_FMT_CNTL,
              0, /* PA_SU_POLY_OFFSET_DB_FMT_CNTL */
              0, /* PA_SU_POLY_OFFSET_CLAMP */
              0, /* PA_SU_POLY_OFFSET_FRONT_SCALE */
              0, /* PA_SU_POLY_OFFSET_FRONT_OFFSET */
              0, /* PA_SU_POLY_OFFSET_BACK_SCALE */
              0); /* PA_SU_POLY_OFFSET_BACK_OFFSET */

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SC_LINE_STIPPLE, 0);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SC_MODE_CNTL, 0);

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SU_LINE_CNTL, 0);

    REG_PACK3(cs, IT_SET_CONTEXT_REG, PA_SU_SC_MODE_CNTL,
              CULL_FRONT_bit | /* PA_SU_SC_MODE_CNTL */
              CULL_BACK_bit |
              FACE_bit |
              (2 << POLYMODE_FRONT_PTYPE_shift) |
              (2 << POLYMODE_BACK_PTYPE_shift));
    MULTI_REG(cs, IT_SET_CONTEXT_REG, PA_SU_POINT_SIZE,
              0, /* PA_SU_POINT_SIZE */
              0); /* PA_SU_POINT_MINMAX */

    /* SPI registers */
    REG_PACK3(cs, IT_SET_CONFIG_REG, SPI_CONFIG_CNTL, 0);
    REG_PACK3(cs, IT_SET_CONFIG_REG, SPI_CONFIG_CNTL_1, X_DELAY_17_CLKS << VTX_DONE_DELAY_shift);

    REG_PACK3(cs, IT_SET_CONTEXT_REG, SPI_PS_INPUT_CNTL_0, 0);
    MULTI_REG(cs, IT_SET_CONTEXT_REG, SPI_INPUT_Z,
              0, /* SPI_INPUT_Z */
              0); /* SPI_FOG_CNTL */
    REG_PACK3(cs, IT_SET_CONTEXT_REG, SPI_VS_OUT_CONFIG, 0);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, SPI_VS_OUT_ID_0, 0);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, SPI_INTERP_CONTROL_0, 0);
    MULTI_REG(cs, IT_SET_CONTEXT_REG, SPI_PS_IN_CONTROL_0,
              LINEAR_GRADIENT_ENA_bit, /* SPI_PS_IN_CONTROL_0 */
              0); /* SPI_PS_IN_CONTROL_1 */

    /* color buffer registers */
    MULTI_REG(cs, IT_SET_CONTEXT_REG, CB_COLOR0_BASE,
              0, 0, 0, 0, 0, 0, 0, 0, /* CB_COLOR[0-7]_BASE */
              0, 0, 0, 0, 0, 0, 0, 0, /* CB_COLOR[0-7]_SIZE */
              0, 0, 0, 0, 0, 0, 0, 0, /* CB_COLOR[0-7]_VIEW */
              0, 0, 0, 0, 0, 0, 0, 0, /* CB_COLOR[0-7]_INFO */
              0, 0, 0, 0, 0, 0, 0, 0, /* CB_COLOR[0-7]_TILE */
              0, 0, 0, 0, 0, 0, 0, 0, /* CB_COLOR[0-7]_FRAG */
              0, 0, 0, 0, 0, 0, 0, 0); /* CB_COLOR[0-7]_MASK */

    /* Set a render target */
    radeon_cs_space_add_persistent_bo(cs, bo2, 0, RADEON_GEM_DOMAIN_VRAM);
    if(radeon_cs_space_check(cs) != 0) {
        fputs("Space check failed\n", stderr);
        rval = 1;
        goto cleanup;
    }

#define CB_SIZE 0
    REG_RELOC(cs, IT_SET_CONTEXT_REG, CB_COLOR0_BASE, 0 >> 8,
              bo2, 0, RADEON_GEM_DOMAIN_VRAM);
    REG_RELOC(cs, IT_SET_CONTEXT_REG, CB_COLOR0_TILE, 0 >> 8,
              bo2, 0, RADEON_GEM_DOMAIN_VRAM);
    REG_RELOC(cs, IT_SET_CONTEXT_REG, CB_COLOR0_FRAG, 0 >> 8,
              bo2, 0, RADEON_GEM_DOMAIN_VRAM);
    REG_RELOC(cs, IT_SET_CONTEXT_REG, CB_COLOR0_INFO, 0,
              bo2, 0, RADEON_GEM_DOMAIN_VRAM);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, CB_COLOR0_SIZE, CB_SIZE);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, CB_COLOR0_VIEW, 0);
    REG_PACK3(cs, IT_SET_CONTEXT_REG, CB_COLOR0_MASK, 0);

    CAB(cs, 7);
    unchecked_packet3(cs, IT_SURFACE_SYNC, 4);
    rad_cwd(cs, CB_ACTION_ENA_bit);
    rad_cwd(cs, (CB_SIZE + 255) >> 8);
    rad_cwd(cs, 0 >> 8);
    rad_cwd(cs, 10);
    RELOC(cs, bo2, 0, RADEON_GEM_DOMAIN_VRAM);
    END(cs);

    if((radeon_bo_map(bo2, 0) != 0) || (bo2->ptr == NULL)) {
        fputs("Could not map second buffer object into main memory\n", stderr);
        rval = 1;
        goto cleanup;
    } else {
        bo2_mapped = 1;
        fputs("Buffer object 2 mapped\n", stderr);
        print_bo_info(bo2);
    }

    fputs(HLINE("BUFFER 2"), stderr);
    print_buffers(x, (unsigned char *) bo2->ptr, BUF_SIZE);

    fputs("End!\n", stderr);

cleanup:

    if(cs != NULL)
        radeon_cs_destroy(cs);

    if(shader != NULL)
        shader = radeon_bo_unref(shader);

    if(bo_mapped)
        radeon_bo_unmap(bo);

    if(bo != NULL)
        bo = radeon_bo_unref(bo);

    if(bo2_mapped)
        radeon_bo_unmap(bo2);

    if(bo2 != NULL)
        bo2 = radeon_bo_unref(bo2);

    if(cmdmgr != NULL)
        radeon_cs_manager_gem_dtor(cmdmgr);

    if(bufmgr != NULL)
        radeon_bo_manager_gem_dtor(bufmgr);

    if(drm_fd >= 0)
        drmClose(drm_fd);

    return rval;
}
