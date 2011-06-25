/**
 * step01.c: setup/teardown of DRM interface
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
