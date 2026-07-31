#include "assets.h"

const void *ltm_asset_get(int resource_id, size_t *len)
{
    HMODULE  mod = GetModuleHandleW(NULL);
    HRSRC    res;
    HGLOBAL  handle;
    const void *ptr;

    if (len != NULL) {
        *len = 0;
    }
    res = FindResourceW(mod, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (res == NULL) {
        return NULL;
    }
    handle = LoadResource(mod, res);
    if (handle == NULL) {
        return NULL;
    }
    ptr = LockResource(handle);
    if (ptr == NULL) {
        return NULL;
    }
    if (len != NULL) {
        *len = (size_t)SizeofResource(mod, res);
    }
    return ptr;
}
