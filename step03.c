/**
 * step03.c
 *
 * Copyright © 2011 Zachary Catlin <z@zc.is>
 *
 * All Rights Reserved.
 * (If this pans out, I'll make the end result open-source)
 */

#include <inttypes.h>
#include <stdio.h>

#include <xf86drm.h>
#include <radeon_drm.h>
#include <radeon_bo.h>
#include <radeon_bo_gem.h>

#define BUF_SIZE 256

#define HLINE(s) "---------------- " s " ----------------\n"

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

int main(int argc, char **argv)
{
    int drm_fd = -1, rval = 0;
    drmSetVersion sv;
    struct radeon_bo_manager *bufmgr = NULL;
    int bo_mapped = 0, bo2_mapped = 0;
    struct radeon_bo *bo = NULL, *bo2 = NULL;
    struct drm_radeon_gem_info meminfo;
    size_t i;
    unsigned char *ptr = NULL;

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

    if(!drmCommandWriteRead(drm_fd, DRM_RADEON_GEM_INFO, &meminfo, sizeof(meminfo)))
        fprintf(stderr,
                "GART size: %8" PRIx64 "\nVRAM size: %8" PRIx64
                "\n VRAM vis: %8" PRIx64 "\n",
                meminfo.gart_size, meminfo.vram_size, meminfo.vram_visible);
    else
        fputs("Ioctl DRM_RADEON_GEM_INFO failed\n", stderr);

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

    if(bo_mapped)
        radeon_bo_unmap(bo);

    if(bo != NULL)
        bo = radeon_bo_unref(bo);

    if(bo2_mapped)
        radeon_bo_unmap(bo2);

    if(bo2 != NULL)
        bo2 = radeon_bo_unref(bo2);

    if(bufmgr != NULL)
        radeon_bo_manager_gem_dtor(bufmgr);

    if(drm_fd >= 0)
        drmClose(drm_fd);

    return rval;
}
