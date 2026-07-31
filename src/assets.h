/*
 * assets.h - access to the web front-end embedded in the executable.
 *
 * The HTML/CSS/JS live in the PE image as RCDATA. LockResource hands back a
 * pointer straight into the memory-mapped image, so serving a page involves
 * no file I/O, no allocation and no copy: the bytes go from the mapping to
 * the socket.
 */
#ifndef LTM_ASSETS_H
#define LTM_ASSETS_H

#include "common.h"

/* Returns NULL if the resource is missing. `len` receives the byte count. */
const void *ltm_asset_get(int resource_id, size_t *len);

#endif /* LTM_ASSETS_H */
