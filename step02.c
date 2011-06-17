/**
 * step02.c
 *
 * Copyright © 2011 Zachary Catlin <z@zc.is>
 *
 * All Rights Reserved.
 * (If this pans out, I'll make the end result open-source)
 */

#include <stdio.h>
#include <stdint.h>

#include <xf86drm.h>
#include <radeon_drm.h>
#include <radeon_bo.h>
#include <radeon_bo_gem.h>

int main(int argc, char **argv)
{
    int drm_fd = -1, rval = 0;
    drmSetVersion sv;
    struct radeon_bo_manager *bufmgr = NULL;
    struct radeon_bo *bo = NULL;
    struct drm_radeon_gem_info meminfo;

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
                "GART size: %8lx\nVRAM size: %8lx\n VRAM vis: %8lx\n",
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

    fputs("End!\n", stderr);

cleanup:

    if(bo != NULL)
        bo = radeon_bo_unref(bo);

    if(bufmgr != NULL)
        radeon_bo_manager_gem_dtor(bufmgr);

    if(drm_fd >= 0)
        drmClose(drm_fd);

    return rval;
}
