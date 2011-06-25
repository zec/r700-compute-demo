/**
 * step02.c
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

#include <xf86drm.h>
#include <radeon_drm.h>
#include <radeon_bo.h>
#include <radeon_bo_gem.h>

#define BUF_SIZE 256

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

int main(int argc, char **argv)
{
    int drm_fd = -1, rval = 0;
    drmSetVersion sv;
    struct radeon_bo_manager *bufmgr = NULL;
    int bo_mapped = 0;
    struct radeon_bo *bo = NULL;
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
    if((bo = radeon_bo_open(bufmgr, /* buffer manager */
                            0,      /* handle (0 for new) */
                            256,    /* size (in bytes) */
                            4096,   /* alignment (in bytes) */
                            RADEON_GEM_DOMAIN_VRAM, /* memory domain */
                            0))     /* flags */
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

    fputs("        original        |      buffer object\n", stderr);
#define B "%02x "
    for(i = 0; i < BUF_SIZE / 8; i++)
        fprintf(stderr, B B B B B B B B "  " B B B B B B B B "\n",
                x[i*8], x[i*8+1], x[i*8+2], x[i*8+3],
                x[i*8+4], x[i*8+5], x[i*8+6], x[i*8+7],
                ptr[i*8], ptr[i*8+1], ptr[i*8+2], ptr[i*8+3],
                ptr[i*8+4], ptr[i*8+5], ptr[i*8+6], ptr[i*8+7]);

    fputs("End!\n", stderr);

cleanup:

    if(bo_mapped)
        radeon_bo_unmap(bo);

    if(bo != NULL)
        bo = radeon_bo_unref(bo);

    if(bufmgr != NULL)
        radeon_bo_manager_gem_dtor(bufmgr);

    if(drm_fd >= 0)
        drmClose(drm_fd);

    return rval;
}
