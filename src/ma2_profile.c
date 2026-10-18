/*
 * MA2_PROFILE=1: a sampling profiler for the recompiled title.
 *
 * Every millisecond, for MA2_PROFILE_SECS seconds starting MA2_PROFILE_DELAY
 * seconds after boot, each thread of the process is suspended, its RIP read
 * and the thread resumed. Samples are counted per (thread, RIP) and written to
 * MA2_PROFILE_OUT (default analysis\profile.txt) as module offsets, which
 * scripts/profile_map.py turns into function names using ma2.map.
 *
 * There is no PDB for 2.7 million lines of generated C, and a full debug
 * build would double the build time; RIP plus the linker map is enough to
 * name the hot guest functions and runtime routines.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define PROF_MAX_THREADS 64
#define PROF_SLOTS       (1u << 16)

typedef struct { uint64_t rip; uint32_t count; } ProfSlot;

typedef struct {
    DWORD tid;
    HANDLE h;
    uint64_t samples;
    ProfSlot *slots;
} ProfThread;

static ProfThread s_threads[PROF_MAX_THREADS];
static int s_nthreads;

static void prof_count(ProfThread *t, uint64_t rip)
{
    uint32_t i = (uint32_t)((rip * 0x9E3779B97F4A7C15ull) >> 48) & (PROF_SLOTS - 1);
    for (;;) {
        ProfSlot *s = &t->slots[i];
        if (s->rip == rip) { s->count++; return; }
        if (s->rip == 0) { s->rip = rip; s->count = 1; return; }
        i = (i + 1) & (PROF_SLOTS - 1);
    }
}

static int cmp_slot(const void *a, const void *b)
{
    const ProfSlot *x = a, *y = b;
    return (x->count < y->count) - (x->count > y->count);
}

static void prof_refresh_threads(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();

    if (snap == INVALID_HANDLE_VALUE)
        return;
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) {
        do {
            int k, known = 0;
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
                continue;
            for (k = 0; k < s_nthreads; k++)
                if (s_threads[k].tid == te.th32ThreadID) { known = 1; break; }
            if (!known && s_nthreads < PROF_MAX_THREADS) {
                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                      THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (h) {
                    s_threads[s_nthreads].tid = te.th32ThreadID;
                    s_threads[s_nthreads].h = h;
                    s_threads[s_nthreads].slots = calloc(PROF_SLOTS, sizeof(ProfSlot));
                    s_nthreads++;
                }
            }
            te.dwSize = sizeof te;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static DWORD WINAPI prof_thread(LPVOID unused)
{
    const char *d = getenv("MA2_PROFILE_DELAY"), *s = getenv("MA2_PROFILE_SECS");
    const char *out = getenv("MA2_PROFILE_OUT");
    DWORD delay = d ? (DWORD)atoi(d) : 60, secs = s ? (DWORD)atoi(s) : 20;
    uint64_t base = (uint64_t)(uintptr_t)GetModuleHandleA(NULL);
    DWORD start, last_refresh = 0;
    FILE *f;
    int k;

    (void)unused;
    timeBeginPeriod(1);
    Sleep(delay * 1000);
    start = GetTickCount();
    while (GetTickCount() - start < secs * 1000) {
        if (GetTickCount() - last_refresh > 1000) {
            prof_refresh_threads();
            last_refresh = GetTickCount();
        }
        for (k = 0; k < s_nthreads; k++) {
            CONTEXT c;
            if (SuspendThread(s_threads[k].h) == (DWORD)-1)
                continue;
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(s_threads[k].h, &c)) {
                prof_count(&s_threads[k], c.Rip);
                s_threads[k].samples++;
            }
            ResumeThread(s_threads[k].h);
        }
        Sleep(1);
    }

    f = fopen(out ? out : "analysis\\profile.txt", "w");
    if (!f)
        return 0;
    fprintf(f, "# module base 0x%llX, %u s\n", (unsigned long long)base, secs);
    for (k = 0; k < s_nthreads; k++) {
        ProfThread *t = &s_threads[k];
        uint32_t n = 0, i;
        ProfSlot *flat;
        if (t->samples < 50)
            continue;
        flat = malloc(PROF_SLOTS * sizeof *flat);
        for (i = 0; i < PROF_SLOTS; i++)
            if (t->slots[i].rip)
                flat[n++] = t->slots[i];
        qsort(flat, n, sizeof *flat, cmp_slot);
        fprintf(f, "thread %lu samples %llu\n", (unsigned long)t->tid,
                (unsigned long long)t->samples);
        for (i = 0; i < n && i < 400; i++) {
            uint64_t r = flat[i].rip;
            if (r >= base && r < base + 0x10000000ull) {
                fprintf(f, "  %u exe+0x%llX\n", flat[i].count, (unsigned long long)(r - base));
            } else {
                HMODULE m = NULL;
                char path[MAX_PATH] = "?", *nm;
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)(uintptr_t)r, &m) && m)
                    GetModuleFileNameA(m, path, sizeof path);
                nm = strrchr(path, '\\');
                fprintf(f, "  %u mod %s+0x%llX\n", flat[i].count, nm ? nm + 1 : path,
                        (unsigned long long)(r - (uint64_t)(uintptr_t)m));
            }
        }
        free(flat);
    }
    fclose(f);
    fprintf(stderr, "[MA2] profile written (%d threads)\n", s_nthreads);
    return 0;
}

void ma2_ProfileStart(void)
{
    HANDLE h;
    if (!getenv("MA2_PROFILE"))
        return;
    h = CreateThread(NULL, 0, prof_thread, NULL, 0, NULL);
    if (h) {
        SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL);
        CloseHandle(h);
    }
}
