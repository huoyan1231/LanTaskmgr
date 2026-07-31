#include "procs.h"
#include "logging.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* ntdll: SystemProcessInformation                                     */
/*                                                                     */
/* One call returns name, pid, session, CPU times and working set for   */
/* every process on the machine. The documented alternative (Toolhelp32 */
/* plus an OpenProcess/GetProcessMemoryInfo/GetProcessTimes trio per     */
/* pid) costs several hundred handle opens every two seconds, which is   */
/* exactly the kind of overhead this rewrite exists to avoid. It is kept */
/* as a fallback in case the native call ever refuses.                   */
/* ------------------------------------------------------------------ */

#define LTM_SystemProcessInformation      5
#define LTM_STATUS_INFO_LENGTH_MISMATCH   ((LONG)0xC0000004L)

typedef struct LTM_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} LTM_UNICODE_STRING;

typedef struct LTM_SYSTEM_PROCESS_INFORMATION {
    ULONG              NextEntryOffset;
    ULONG              NumberOfThreads;
    LARGE_INTEGER      WorkingSetPrivateSize;
    ULONG              HardFaultCount;
    ULONG              NumberOfThreadsHighWatermark;
    ULONGLONG          CycleTime;
    LARGE_INTEGER      CreateTime;
    LARGE_INTEGER      UserTime;
    LARGE_INTEGER      KernelTime;
    LTM_UNICODE_STRING ImageName;
    LONG               BasePriority;
    HANDLE             UniqueProcessId;
    HANDLE             InheritedFromUniqueProcessId;
    ULONG              HandleCount;
    ULONG              SessionId;
    ULONG_PTR          UniqueProcessKey;
    SIZE_T             PeakVirtualSize;
    SIZE_T             VirtualSize;
    ULONG              PageFaultCount;
    SIZE_T             PeakWorkingSetSize;
    SIZE_T             WorkingSetSize;
    SIZE_T             QuotaPeakPagedPoolUsage;
    SIZE_T             QuotaPagedPoolUsage;
    SIZE_T             QuotaPeakNonPagedPoolUsage;
    SIZE_T             QuotaNonPagedPoolUsage;
    SIZE_T             PagefileUsage;
    SIZE_T             PeakPagefileUsage;
    SIZE_T             PrivatePageCount;
    LARGE_INTEGER      ReadOperationCount;
    LARGE_INTEGER      WriteOperationCount;
    LARGE_INTEGER      OtherOperationCount;
    LARGE_INTEGER      ReadTransferCount;
    LARGE_INTEGER      WriteTransferCount;
    LARGE_INTEGER      OtherTransferCount;
} LTM_SYSTEM_PROCESS_INFORMATION;

typedef LONG(NTAPI *PFN_NtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);
static PFN_NtQuerySystemInformation g_NtQuerySystemInformation;

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

typedef struct raw_proc {
    DWORD   pid;
    DWORD   session;
    ULONG64 cpu100ns;
    ULONG64 mem;
    WCHAR   name[LTM_PROC_NAME_MAX];
} raw_proc;

typedef struct cpu_prev {
    DWORD   pid;
    ULONG64 cpu100ns;
} cpu_prev;

typedef struct win_title {
    DWORD pid;
    WCHAR title[LTM_PROC_TITLE_MAX];
} win_title;

static CRITICAL_SECTION g_lock;
static BOOL             g_ready;

/* Reused across snapshots so the steady state performs no allocation at all. */
static void    *g_sysbuf;
static SIZE_T   g_sysbuf_cap;
static cpu_prev *g_prev;
static int      g_prev_count;
static int      g_prev_cap;
static ULONG64  g_prev_time100ns;
static DWORD    g_cpu_count = 1;

/* Terminating any of these takes the whole machine down immediately. The
 * original merely warned; refusing outright costs nothing and the user has no
 * legitimate reason to reach for them from a phone. */
static const WCHAR *const kProtected[] = {
    L"system", L"system idle process", L"idle", L"registry",
    L"memory compression", L"smss.exe", L"csrss.exe", L"wininit.exe",
    L"services.exe", L"lsass.exe", L"lsaiso.exe", L"winlogon.exe"
};

BOOL ltm_proc_is_protected_name(const WCHAR *name)
{
    size_t i;
    if (name == NULL) {
        return TRUE;
    }
    for (i = 0; i < LTM_COUNTOF(kProtected); ++i) {
        if (_wcsicmp(name, kProtected[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

void ltm_proc_init(void)
{
    SYSTEM_INFO si;
    HMODULE     ntdll;

    if (g_ready) {
        return;
    }
    InitializeCriticalSection(&g_lock);

    GetSystemInfo(&si);
    g_cpu_count = (si.dwNumberOfProcessors > 0) ? si.dwNumberOfProcessors : 1;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != NULL) {
        g_NtQuerySystemInformation = (PFN_NtQuerySystemInformation)(void *)
            GetProcAddress(ntdll, "NtQuerySystemInformation");
    }
    g_ready = TRUE;
}

void ltm_proc_shutdown(void)
{
    if (!g_ready) {
        return;
    }
    g_ready = FALSE;
    ltm_free(g_sysbuf);
    g_sysbuf = NULL;
    g_sysbuf_cap = 0;
    ltm_free(g_prev);
    g_prev = NULL;
    g_prev_count = g_prev_cap = 0;
    DeleteCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Visible window titles                                               */
/* ------------------------------------------------------------------ */

typedef struct enum_ctx {
    win_title *items;
    int        count;
    int        cap;
} enum_ctx;

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lparam)
{
    enum_ctx *ctx = (enum_ctx *)lparam;
    DWORD     pid = 0;
    int       len;
    int       i;

    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != NULL) {
        return TRUE;
    }
    len = GetWindowTextLengthW(hwnd);
    if (len <= 0) {
        return TRUE;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return TRUE;
    }
    for (i = 0; i < ctx->count; ++i) {
        if (ctx->items[i].pid == pid) {
            return TRUE; /* first caption wins */
        }
    }
    if (ctx->count == ctx->cap) {
        int        ncap = (ctx->cap == 0) ? 32 : ctx->cap * 2;
        win_title *ni = (win_title *)ltm_realloc(ctx->items, (size_t)ncap * sizeof(win_title));
        if (ni == NULL) {
            return FALSE;
        }
        ctx->items = ni;
        ctx->cap = ncap;
    }
    ctx->items[ctx->count].pid = pid;
    GetWindowTextW(hwnd, ctx->items[ctx->count].title, LTM_PROC_TITLE_MAX);
    ctx->items[ctx->count].title[LTM_PROC_TITLE_MAX - 1] = L'\0';
    ctx->count++;
    return TRUE;
}

static const WCHAR *find_title(const enum_ctx *ctx, DWORD pid)
{
    int i;
    for (i = 0; i < ctx->count; ++i) {
        if (ctx->items[i].pid == pid) {
            return ctx->items[i].title;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Raw sampling                                                        */
/* ------------------------------------------------------------------ */

static BOOL raw_push(raw_proc **arr, int *count, int *cap, const raw_proc *p)
{
    if (*count == *cap) {
        int       ncap = (*cap == 0) ? 256 : *cap * 2;
        raw_proc *na = (raw_proc *)ltm_realloc(*arr, (size_t)ncap * sizeof(raw_proc));
        if (na == NULL) {
            return FALSE;
        }
        *arr = na;
        *cap = ncap;
    }
    (*arr)[(*count)++] = *p;
    return TRUE;
}

static int sample_native(raw_proc **out, int *out_count)
{
    LONG   status;
    ULONG  needed = 0;
    raw_proc *arr = NULL;
    int    count = 0, cap = 0;
    const LTM_SYSTEM_PROCESS_INFORMATION *spi;
    const BYTE *cursor;

    if (g_NtQuerySystemInformation == NULL) {
        return FALSE;
    }
    if (g_sysbuf == NULL) {
        g_sysbuf_cap = 256 * 1024;
        g_sysbuf = ltm_alloc(g_sysbuf_cap);
        if (g_sysbuf == NULL) {
            g_sysbuf_cap = 0;
            return FALSE;
        }
    }

    for (;;) {
        status = g_NtQuerySystemInformation(LTM_SystemProcessInformation, g_sysbuf,
                                            (ULONG)g_sysbuf_cap, &needed);
        if (status >= 0) {
            break;
        }
        if (status != LTM_STATUS_INFO_LENGTH_MISMATCH) {
            return FALSE;
        }
        {
            SIZE_T ncap = (needed > 0) ? ((SIZE_T)needed + 64 * 1024) : (g_sysbuf_cap * 2);
            void  *nb;
            if (ncap > 64u * 1024u * 1024u) {
                return FALSE;
            }
            nb = ltm_realloc(g_sysbuf, ncap);
            if (nb == NULL) {
                return FALSE;
            }
            g_sysbuf = nb;
            g_sysbuf_cap = ncap;
        }
    }

    cursor = (const BYTE *)g_sysbuf;
    for (;;) {
        raw_proc rp;
        spi = (const LTM_SYSTEM_PROCESS_INFORMATION *)cursor;

        ZeroMemory(&rp, sizeof(rp));
        rp.pid = (DWORD)(ULONG_PTR)spi->UniqueProcessId;
        rp.session = spi->SessionId;
        rp.cpu100ns = (ULONG64)spi->KernelTime.QuadPart + (ULONG64)spi->UserTime.QuadPart;
        rp.mem = (spi->WorkingSetPrivateSize.QuadPart > 0)
                     ? (ULONG64)spi->WorkingSetPrivateSize.QuadPart
                     : (ULONG64)spi->WorkingSetSize;

        if (spi->ImageName.Buffer != NULL && spi->ImageName.Length > 0) {
            int chars = spi->ImageName.Length / (int)sizeof(WCHAR);
            if (chars > LTM_PROC_NAME_MAX - 1) {
                chars = LTM_PROC_NAME_MAX - 1;
            }
            memcpy(rp.name, spi->ImageName.Buffer, (size_t)chars * sizeof(WCHAR));
            rp.name[chars] = L'\0';
        } else if (rp.pid == 0) {
            ltm_strlcpy_w(rp.name, LTM_PROC_NAME_MAX, L"System Idle Process");
        } else {
            ltm_strlcpy_w(rp.name, LTM_PROC_NAME_MAX, L"System");
        }

        /* The idle process is an accounting artefact, not something to show. */
        if (rp.pid != 0) {
            if (!raw_push(&arr, &count, &cap, &rp)) {
                ltm_free(arr);
                return FALSE;
            }
        }

        if (spi->NextEntryOffset == 0) {
            break;
        }
        cursor += spi->NextEntryOffset;
    }

    *out = arr;
    *out_count = count;
    return TRUE;
}

static int sample_toolhelp(raw_proc **out, int *out_count)
{
    HANDLE          snap;
    PROCESSENTRY32W pe;
    raw_proc       *arr = NULL;
    int             count = 0, cap = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            raw_proc rp;
            HANDLE   h;

            if (pe.th32ProcessID == 0) {
                continue;
            }
            ZeroMemory(&rp, sizeof(rp));
            rp.pid = pe.th32ProcessID;
            ltm_strlcpy_w(rp.name, LTM_PROC_NAME_MAX, pe.szExeFile);
            ProcessIdToSessionId(rp.pid, &rp.session);

            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, rp.pid);
            if (h != NULL) {
                PROCESS_MEMORY_COUNTERS pmc;
                FILETIME ct, et, kt, ut;
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                    rp.mem = pmc.WorkingSetSize;
                }
                if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
                    ULARGE_INTEGER k, u;
                    k.LowPart = kt.dwLowDateTime;  k.HighPart = kt.dwHighDateTime;
                    u.LowPart = ut.dwLowDateTime;  u.HighPart = ut.dwHighDateTime;
                    rp.cpu100ns = k.QuadPart + u.QuadPart;
                }
                CloseHandle(h);
            }
            if (!raw_push(&arr, &count, &cap, &rp)) {
                ltm_free(arr);
                CloseHandle(snap);
                return FALSE;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    *out = arr;
    *out_count = count;
    return count > 0;
}

/* ------------------------------------------------------------------ */
/* CPU deltas                                                          */
/* ------------------------------------------------------------------ */

static int cmp_prev(const void *a, const void *b)
{
    DWORD pa = ((const cpu_prev *)a)->pid;
    DWORD pb = ((const cpu_prev *)b)->pid;
    return (pa < pb) ? -1 : (pa > pb) ? 1 : 0;
}

static ULONG64 prev_cpu_for(DWORD pid, BOOL *found)
{
    int lo = 0, hi = g_prev_count - 1;
    *found = FALSE;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_prev[mid].pid == pid) {
            *found = TRUE;
            return g_prev[mid].cpu100ns;
        }
        if (g_prev[mid].pid < pid) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}

static void remember_cpu(const raw_proc *raw, int count, ULONG64 now100ns)
{
    int i;
    if (count > g_prev_cap) {
        int       ncap = count + 64;
        cpu_prev *np = (cpu_prev *)ltm_realloc(g_prev, (size_t)ncap * sizeof(cpu_prev));
        if (np == NULL) {
            return;
        }
        g_prev = np;
        g_prev_cap = ncap;
    }
    for (i = 0; i < count; ++i) {
        g_prev[i].pid = raw[i].pid;
        g_prev[i].cpu100ns = raw[i].cpu100ns;
    }
    g_prev_count = count;
    qsort(g_prev, (size_t)g_prev_count, sizeof(cpu_prev), cmp_prev);
    g_prev_time100ns = now100ns;
}

/* ------------------------------------------------------------------ */
/* Grouping                                                            */
/* ------------------------------------------------------------------ */

static int cmp_group_name(const void *a, const void *b)
{
    return _wcsicmp(((const ltm_proc_group *)a)->name, ((const ltm_proc_group *)b)->name);
}

static int cmp_raw_name(const void *a, const void *b)
{
    const raw_proc *ra = (const raw_proc *)a;
    const raw_proc *rb = (const raw_proc *)b;
    int c = _wcsicmp(ra->name, rb->name);
    if (c != 0) {
        return c;
    }
    return (ra->pid < rb->pid) ? -1 : (ra->pid > rb->pid) ? 1 : 0;
}

BOOL ltm_proc_snapshot_take(ltm_proc_snapshot *out)
{
    raw_proc       *raw = NULL;
    int             raw_count = 0;
    enum_ctx        wins;
    ltm_proc_group *groups = NULL;
    int             gcount = 0;
    FILETIME        ft;
    ULARGE_INTEGER  now;
    ULONG64         wall_delta;
    int             i;

    out->items = NULL;
    out->count = 0;
    if (!g_ready) {
        return FALSE;
    }

    ZeroMemory(&wins, sizeof(wins));
    EnumWindows(enum_windows_cb, (LPARAM)&wins);

    EnterCriticalSection(&g_lock);

    if (!sample_native(&raw, &raw_count)) {
        if (!sample_toolhelp(&raw, &raw_count)) {
            LeaveCriticalSection(&g_lock);
            ltm_free(wins.items);
            return FALSE;
        }
    }

    GetSystemTimeAsFileTime(&ft);
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    wall_delta = (g_prev_time100ns != 0 && now.QuadPart > g_prev_time100ns)
                     ? (now.QuadPart - g_prev_time100ns)
                     : 0;

    qsort(raw, (size_t)raw_count, sizeof(raw_proc), cmp_raw_name);

    groups = (ltm_proc_group *)ltm_alloc((size_t)(raw_count + 1) * sizeof(ltm_proc_group));
    if (groups == NULL) {
        ltm_free(raw);
        LeaveCriticalSection(&g_lock);
        ltm_free(wins.items);
        return FALSE;
    }

    for (i = 0; i < raw_count; ++i) {
        const raw_proc *rp = &raw[i];
        ltm_proc_group *g;
        const WCHAR    *title;
        ULONG64         prev;
        BOOL            had_prev;

        if (gcount > 0 && _wcsicmp(groups[gcount - 1].name, rp->name) == 0) {
            g = &groups[gcount - 1];
        } else {
            g = &groups[gcount++];
            ZeroMemory(g, sizeof(*g));
            ltm_strlcpy_w(g->name, LTM_PROC_NAME_MAX, rp->name);
            g->pid = rp->pid;
            g->is_protected = ltm_proc_is_protected_name(rp->name);
            g->klass = g->is_protected ? LTM_PCLASS_SYSTEM : LTM_PCLASS_NORMAL;
        }

        g->instances++;
        g->mem_bytes += rp->mem;

        prev = prev_cpu_for(rp->pid, &had_prev);
        if (had_prev && wall_delta > 0 && rp->cpu100ns > prev) {
            double pct = (double)(rp->cpu100ns - prev) * 100.0 /
                         ((double)wall_delta * (double)g_cpu_count);
            if (pct > 100.0) {
                pct = 100.0;
            }
            g->cpu_pct += (float)pct;
        }

        if (!g->is_protected) {
            title = find_title(&wins, rp->pid);
            if (title != NULL) {
                g->klass = LTM_PCLASS_WINDOW;
                if (g->title[0] == L'\0') {
                    ltm_strlcpy_w(g->title, LTM_PROC_TITLE_MAX, title);
                }
            } else if (rp->session == 0 && g->klass != LTM_PCLASS_WINDOW) {
                g->klass = LTM_PCLASS_SYSTEM;
            }
        }
    }

    remember_cpu(raw, raw_count, now.QuadPart);
    LeaveCriticalSection(&g_lock);

    ltm_free(raw);
    ltm_free(wins.items);

    qsort(groups, (size_t)gcount, sizeof(ltm_proc_group), cmp_group_name);

    out->items = groups;
    out->count = gcount;
    return TRUE;
}

void ltm_proc_snapshot_free(ltm_proc_snapshot *s)
{
    if (s == NULL) {
        return;
    }
    ltm_free(s->items);
    s->items = NULL;
    s->count = 0;
}

/* ------------------------------------------------------------------ */
/* Termination                                                         */
/* ------------------------------------------------------------------ */

ltm_kill_result ltm_proc_kill_by_name(const WCHAR *name, int *killed_out)
{
    HANDLE          snap;
    PROCESSENTRY32W pe;
    int             matched = 0, killed = 0, denied = 0;
    DWORD           self = GetCurrentProcessId();

    if (killed_out != NULL) {
        *killed_out = 0;
    }
    if (name == NULL || name[0] == L'\0') {
        return LTM_KILL_NOT_FOUND;
    }
    if (ltm_proc_is_protected_name(name)) {
        ltm_log(L"refused to terminate protected process '%s'", name);
        return LTM_KILL_PROTECTED;
    }

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return LTM_KILL_DENIED;
    }
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE h;
            if (_wcsicmp(pe.szExeFile, name) != 0) {
                continue;
            }
            if (pe.th32ProcessID == self || pe.th32ProcessID == 0) {
                continue;
            }
            matched++;
            h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (h == NULL) {
                denied++;
                continue;
            }
            if (TerminateProcess(h, 1)) {
                killed++;
            } else {
                denied++;
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (killed_out != NULL) {
        *killed_out = killed;
    }

    if (matched == 0) {
        ltm_log(L"kill '%s': no such process", name);
        return LTM_KILL_NOT_FOUND;
    }
    if (killed == 0) {
        ltm_log(L"kill '%s': access denied on all %d instance(s)", name, matched);
        return LTM_KILL_DENIED;
    }
    ltm_log(L"kill '%s': terminated %d of %d instance(s)", name, killed, matched);
    return (denied > 0) ? LTM_KILL_PARTIAL : LTM_KILL_OK;
}
