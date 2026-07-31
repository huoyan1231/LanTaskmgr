/*
 * procs.h - process enumeration, classification and termination.
 *
 * Processes are aggregated by image name, the way the original presented them:
 * the phone shows "chrome.exe x14", and killing it kills the whole family.
 */
#ifndef LTM_PROCS_H
#define LTM_PROCS_H

#include "common.h"

#define LTM_PROC_NAME_MAX  64
#define LTM_PROC_TITLE_MAX 128

typedef enum ltm_pclass {
    LTM_PCLASS_NORMAL = 0, /* background process owned by the user   */
    LTM_PCLASS_WINDOW = 1, /* has a visible top level window         */
    LTM_PCLASS_SYSTEM = 2  /* session 0 / OS infrastructure          */
} ltm_pclass;

typedef struct ltm_proc_group {
    WCHAR      name[LTM_PROC_NAME_MAX];
    WCHAR      title[LTM_PROC_TITLE_MAX]; /* representative window caption */
    DWORD      instances;
    DWORD      pid;         /* first pid seen, for display only */
    ULONG64    mem_bytes;   /* summed private working set */
    float      cpu_pct;     /* share of total CPU since the previous snapshot */
    ltm_pclass klass;
    BOOL       is_protected; /* terminating it would bugcheck Windows */
} ltm_proc_group;

typedef struct ltm_proc_snapshot {
    ltm_proc_group *items;
    int             count;
} ltm_proc_snapshot;

typedef enum ltm_kill_result {
    LTM_KILL_OK = 0,
    LTM_KILL_PARTIAL,    /* some instances died, some refused */
    LTM_KILL_NOT_FOUND,
    LTM_KILL_PROTECTED,
    LTM_KILL_DENIED      /* access denied on every instance */
} ltm_kill_result;

void ltm_proc_init(void);
void ltm_proc_shutdown(void);

/* Fills `out` with a freshly sampled, name-sorted list. The caller owns the
 * result and must pass it to ltm_proc_snapshot_free().
 *
 * CPU percentages are computed against the previous call, so the very first
 * snapshot after start-up reports 0 for everything. */
BOOL ltm_proc_snapshot_take(ltm_proc_snapshot *out);
void ltm_proc_snapshot_free(ltm_proc_snapshot *s);

/* Terminates every process whose image name matches (case insensitive).
 * `killed_out` may be NULL. */
ltm_kill_result ltm_proc_kill_by_name(const WCHAR *name, int *killed_out);

/* TRUE for the handful of processes that instantly bugcheck or reboot
 * Windows when terminated. */
BOOL ltm_proc_is_protected_name(const WCHAR *name);

#endif /* LTM_PROCS_H */
