/**
 * step01.c: setup/teardown of DRM interface
 *
 * Copyright © 2011 Zachary Catlin <z@zc.is>
 *
 * All Rights Reserved.
 * (If this pans out, I'll make the end result open-source)
 */

#include <stdio.h>

#include <xf86drm.h>

int main(int argc, char **argv)
{
    int drm_fd = -1, rval = 0;

    fputs("Hello world!\n", stderr);

    /* Using the driver for & bus address of *my* particular card for now... */
    if((drm_fd = drmOpen("radeon", "PCI:2:0:0")) < 0) {
      fprintf(stderr, "Could not open DRM device (return value %d)\n", drm_fd);
      rval = 1;
      goto cleanup;
    }

    fprintf(stderr, "Got fd %d\n", drm_fd);

    fputs("End!\n", stderr);

cleanup:

    if(drm_fd >= 0)
        drmClose(drm_fd);

    return rval;
}
