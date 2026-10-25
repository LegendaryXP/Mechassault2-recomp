/*
 * MechAssault 2: first-run setup -- where the game files are.
 *
 * The recompiled game needs the player's own disc: ma2.xbe and the data
 * files beside it. A launcher (Quiver, a shortcut) starts ma2.exe on its own,
 * from wherever it was unpacked, so the executable finds everything itself:
 *
 *   - The base folder is the working directory when it holds the layout
 *     (game\ma2.xbe, ma2_settings.ini or mkb.ini -- the development tree, or
 *     play.bat), else the executable's folder. It becomes the working
 *     directory: mkb.ini, logs\ and shader_cache\ live there.
 *   - Started without play.bat (RECOMP_PB_EXEC unset), ma2.exe sets the
 *     environment play.bat would have, and writes its log to
 *     logs\<MA2_LOG_NAME or last>_stderr.txt as play.bat does.
 *   - The game files: <base>\game if it holds ma2.xbe, else the folder named
 *     in ma2_settings.ini, else the Game Path Setup window asks for it. That
 *     takes a folder holding ma2.xbe (an extracted disc), or the disc image
 *     itself (.iso / .xiso), which it extracts to <base>\game once. An
 *     optional second folder holds saves and the game's disk cache (default:
 *     %LOCALAPPDATA%\xboxrecomp, where they have always been).
 *
 * ma2.exe --setup (or MA2_SETUP=1, or Shift held at launch) opens the window
 * again to change the paths. MA2_SETUP_ISO=<image> extracts an image without
 * the window (for tests).
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_FILE "ma2_settings.ini"

static char s_base[MAX_PATH];

static int file_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_game(const char *dir)
{
    char p[MAX_PATH];
    if (!dir || !*dir)
        return 0;
    snprintf(p, sizeof p, "%s\\ma2.xbe", dir);
    return file_exists(p);
}

static int ends_with_ci(const char *s, const char *tail)
{
    size_t n = strlen(s), m = strlen(tail);
    return n >= m && !_stricmp(s + n - m, tail);
}

/* ---- base folder, environment, log ---------------------------------------- */

const char *ma2_BaseDir(void) { return s_base; }

static int is_base(const char *dir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\game\\ma2.xbe", dir);
    if (file_exists(p))
        return 1;
    snprintf(p, sizeof p, "%s\\" SETTINGS_FILE, dir);
    if (file_exists(p))
        return 1;
    snprintf(p, sizeof p, "%s\\mkb.ini", dir);
    return file_exists(p);
}

static void resolve_base(void)
{
    char cwd[MAX_PATH], exe[MAX_PATH], up[MAX_PATH];
    char *slash;
    GetCurrentDirectoryA(MAX_PATH, cwd);
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    if ((slash = strrchr(exe, '\\')) != NULL)
        *slash = 0;
    snprintf(up, sizeof up, "%s\\..\\..", exe);     /* build\Release\ma2.exe */
    if (is_base(cwd))
        snprintf(s_base, sizeof s_base, "%s", cwd);
    else if (!is_base(exe) && is_base(up) && GetFullPathNameA(up, MAX_PATH, s_base, NULL))
        ;
    else
        snprintf(s_base, sizeof s_base, "%s", exe);
    SetCurrentDirectoryA(s_base);
}

static void env_default(const char *name, const char *value)
{
    if (!getenv(name)) {
        char kv[MAX_PATH + 64];
        snprintf(kv, sizeof kv, "%s=%s", name, value);
        _putenv(kv);
    }
}

/* play.bat's switches, for a launch without it (see play.bat for each). */
static void default_env(void)
{
    char ini[MAX_PATH];
    if (getenv("RECOMP_PB_EXEC"))
        return;                     /* play.bat or a test script set them */
    env_default("RECOMP_FB_WINDOW", "1");
    env_default("RECOMP_FB_WINDOW_EXIT", "1");
    env_default("RECOMP_WINDOW_SCALE", "2");
    env_default("RECOMP_AC97_READY", "1");
    env_default("RECOMP_PB_EXEC", "1");
    env_default("RECOMP_PB_WALK", "dma");
    env_default("RECOMP_ASYNC_IO", "1");
    env_default("RECOMP_VBLANK", "1");
    env_default("RECOMP_HEAP_RECLAIM", "1");
    env_default("RECOMP_USB", "1");
    env_default("RECOMP_USB_PADS", "1");
    env_default("RECOMP_MKB", "1");
    snprintf(ini, sizeof ini, "%s\\mkb.ini", s_base);
    env_default("RECOMP_MKB_INI", ini);
    env_default("RECOMP_D3D11", "1");
    env_default("RECOMP_RENDER_SCALE", "2.25");
    env_default("RECOMP_D3D11_AF", "16");
    env_default("RECOMP_D3D11_MSAA", "4");
    env_default("RECOMP_APU_SSL_IRQ", "0");
    env_default("RECOMP_APU_EP_MARK", "1");
    env_default("MA2_LOG_NAME", "last");
}

/* A windowed launch has nowhere to print: send stdout and stderr to logs\,
 * as play.bat does. Left alone when either is already redirected. */
static void redirect_log(void)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD t = (h && h != INVALID_HANDLE_VALUE) ? GetFileType(h) : FILE_TYPE_UNKNOWN;
    const char *name = getenv("MA2_LOG_NAME");
    char p[MAX_PATH];
    if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE || t == FILE_TYPE_CHAR)
        return;
    CreateDirectoryA("logs", NULL);
    snprintf(p, sizeof p, "logs\\%s_stderr.txt", name && *name ? name : "last");
    freopen(p, "w", stderr);
    snprintf(p, sizeof p, "logs\\%s_stdout.txt", name && *name ? name : "last");
    freopen(p, "w", stdout);
}

/* ---- settings --------------------------------------------------------------- */

static void settings_load(char *game, char *saves)
{
    char line[MAX_PATH + 32];
    FILE *f = fopen(SETTINGS_FILE, "r");
    game[0] = saves[0] = 0;
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (!strncmp(line, "game=", 5))
            snprintf(game, MAX_PATH, "%s", line + 5);
        else if (!strncmp(line, "saves=", 6))
            snprintf(saves, MAX_PATH, "%s", line + 6);
    }
    fclose(f);
}

static void settings_save(const char *game, const char *saves)
{
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f)
        return;
    fprintf(f, "# MechAssault 2 (PC): where the game files and the saves are.\n"
               "# ma2.exe --setup changes them.\n"
               "game=%s\nsaves=%s\n", game, saves ? saves : "");
    fclose(f);
}

/* ---- XDVDFS (Xbox disc image) extraction -------------------------------------
 * After xboxrecomp's tools/xiso/xdvdfs.py: the game partition sits at one of
 * a few known offsets; its descriptor (sector 32) names the root directory;
 * a directory is a run of 2048-byte sectors of entries (left, right, sector,
 * size, attributes, name length, name), each sector's run ended by 0xFFFF
 * padding. Walked linearly, sector by sector, as the Python reader does. */

#define XSEC 2048
static const uint64_t k_bases[] = { 0, 0xFD90, 0x30600, 0xFD90000, 0x18300000 };

typedef struct {
    HANDLE f;
    uint64_t base;
    uint32_t root_sector, root_size;
    uint64_t total, done;           /* bytes of file data, for progress */
    HWND notify;                    /* WM_APP+1 progress, WM_APP+2 finished */
    char error[256];
} Xiso;

static int read_at(Xiso *x, uint64_t off, void *buf, uint32_t n)
{
    LARGE_INTEGER li;
    DWORD got = 0;
    li.QuadPart = (LONGLONG)off;
    return SetFilePointerEx(x->f, li, NULL, FILE_BEGIN)
        && ReadFile(x->f, buf, n, &got, NULL) && got == n;
}

static int xiso_open(Xiso *x, const char *path)
{
    uint8_t desc[XSEC];
    size_t i;
    memset(x, 0, sizeof *x);
    x->f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (x->f == INVALID_HANDLE_VALUE) {
        snprintf(x->error, sizeof x->error, "Cannot open the disc image.");
        return 0;
    }
    for (i = 0; i < sizeof k_bases / sizeof k_bases[0]; i++)
        if (read_at(x, k_bases[i] + 32 * XSEC, desc, XSEC)
            && !memcmp(desc, "MICROSOFT*XBOX*MEDIA", 20)) {
            x->base = k_bases[i];
            memcpy(&x->root_sector, desc + 0x14, 4);
            memcpy(&x->root_size, desc + 0x18, 4);
            return 1;
        }
    snprintf(x->error, sizeof x->error, "Not an Xbox disc image.");
    CloseHandle(x->f);
    x->f = INVALID_HANDLE_VALUE;
    return 0;
}

/* Calls fn for each entry of a directory. Returns 0 if fn did. */
typedef int (*EntryFn)(Xiso *x, const char *name, uint32_t sector, uint32_t size,
                       int is_dir, void *ctx);

static int xiso_dir(Xiso *x, uint32_t sector, uint32_t size, EntryFn fn, void *ctx)
{
    uint8_t *data;
    uint32_t base, off;
    if (!size || size > (64u << 20))
        return 1;
    if (!(data = (uint8_t *)malloc(size)))
        return 0;
    if (!read_at(x, x->base + (uint64_t)sector * XSEC, data, size)) {
        free(data);
        snprintf(x->error, sizeof x->error, "The disc image is cut short.");
        return 0;
    }
    for (base = 0; base < size; base += XSEC) {
        uint32_t end = size - base < XSEC ? size - base : XSEC;
        for (off = 0; off + 0x0E <= end;) {
            const uint8_t *e = data + base + off;
            uint16_t l, r;
            uint32_t sec, sz;
            uint8_t attr = e[12], nlen = e[13];
            char name[256];
            memcpy(&l, e, 2); memcpy(&r, e + 2, 2);
            memcpy(&sec, e + 4, 4); memcpy(&sz, e + 8, 4);
            if ((l == 0xFFFF && r == 0xFFFF) || nlen == 0 || off + 0x0E + nlen > end)
                break;
            memcpy(name, e + 0x0E, nlen);
            name[nlen] = 0;
            if (!fn(x, name, sec, sz, (attr & 0x10) != 0, ctx)) {
                free(data);
                return 0;
            }
            off += (0x0E + nlen + 3) & ~3u;
        }
    }
    free(data);
    return 1;
}

static int sum_entry(Xiso *x, const char *name, uint32_t sector, uint32_t size,
                     int is_dir, void *ctx)
{
    (void)name; (void)ctx;
    if (is_dir)
        return xiso_dir(x, sector, size, sum_entry, NULL);
    x->total += size;
    return 1;
}

static int find_xbe(Xiso *x, const char *name, uint32_t sector, uint32_t size,
                    int is_dir, void *ctx)
{
    (void)x; (void)sector; (void)size;
    if (!is_dir && !_stricmp(name, "ma2.xbe"))
        *(int *)ctx = 1;
    return 1;
}

static int extract_entry(Xiso *x, const char *name, uint32_t sector, uint32_t size,
                         int is_dir, void *ctx)
{
    const char *dir = (const char *)ctx;
    char path[MAX_PATH];
    if (strchr(name, '\\') || strchr(name, '/') || strstr(name, "..")) {
        snprintf(x->error, sizeof x->error, "Bad file name in the image: %s", name);
        return 0;
    }
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    if (is_dir) {
        CreateDirectoryA(path, NULL);
        return xiso_dir(x, sector, size, extract_entry, path);
    }
    {
        HANDLE out = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
        static uint8_t buf[1 << 20];
        uint64_t off = x->base + (uint64_t)sector * XSEC;
        uint32_t left = size;
        if (out == INVALID_HANDLE_VALUE) {
            snprintf(x->error, sizeof x->error, "Cannot write %s.", path);
            return 0;
        }
        while (left) {
            uint32_t n = left < sizeof buf ? left : (uint32_t)sizeof buf;
            DWORD wrote = 0;
            if (!read_at(x, off, buf, n)) {
                snprintf(x->error, sizeof x->error, "The disc image is cut short (%s).", name);
                CloseHandle(out);
                return 0;
            }
            if (!WriteFile(out, buf, n, &wrote, NULL) || wrote != n) {
                snprintf(x->error, sizeof x->error, "Cannot write %s (disk full?).", path);
                CloseHandle(out);
                return 0;
            }
            off += n;
            left -= n;
            /* a progress message every 16 MB */
            if (x->notify && ((x->done + n) >> 24) != (x->done >> 24))
                PostMessageA(x->notify, WM_APP + 1, (WPARAM)((x->done + n) >> 20),
                             (LPARAM)(x->total >> 20));
            x->done += n;
        }
        CloseHandle(out);
    }
    return 1;
}

/* Extract the image's files to <base>\game, through game.partial so a
 * half-finished copy is never taken for the game. 1 on success. */
static int xiso_extract(const char *iso, HWND notify, char *error, size_t errlen)
{
    Xiso x;
    char tmp[MAX_PATH], dst[MAX_PATH];
    int ok, found = 0;
    if (!xiso_open(&x, iso)) {
        snprintf(error, errlen, "%s", x.error);
        return 0;
    }
    x.notify = notify;
    xiso_dir(&x, x.root_sector, x.root_size, find_xbe, &found);
    if (!found) {
        CloseHandle(x.f);
        snprintf(error, errlen, "This disc image has no ma2.xbe: not MechAssault 2?");
        return 0;
    }
    x.total = 0;
    xiso_dir(&x, x.root_sector, x.root_size, sum_entry, NULL);
    snprintf(tmp, sizeof tmp, "%s\\game.partial", s_base);
    snprintf(dst, sizeof dst, "%s\\game", s_base);
    CreateDirectoryA(tmp, NULL);
    ok = xiso_dir(&x, x.root_sector, x.root_size, extract_entry, tmp);
    CloseHandle(x.f);
    if (!ok) {
        snprintf(error, errlen, "%s", x.error[0] ? x.error : "Extraction failed.");
        return 0;
    }
    if (dir_exists(dst)) {          /* set aside, never deleted */
        char old[MAX_PATH];
        snprintf(old, sizeof old, "%s\\game_old_%lu", s_base, (unsigned long)GetTickCount());
        if (!MoveFileA(dst, old)) {
            snprintf(error, errlen, "A folder named game is in the way: %s", dst);
            return 0;
        }
    }
    if (!MoveFileA(tmp, dst)) {
        snprintf(error, errlen, "Could not rename %s to %s.", tmp, dst);
        return 0;
    }
    error[0] = 0;
    return 1;
}

/* ---- the window -------------------------------------------------------------- */

enum { ID_GAME = 100, ID_SAVES, ID_BROWSE_DIR, ID_BROWSE_ISO, ID_BROWSE_SAVES,
       ID_CONTINUE, ID_QUIT };

static HWND s_wnd, s_game, s_saves, s_cont;
static HFONT s_font, s_font_title;
static HBRUSH s_bg, s_edit_bg, s_panel_bg;
static int s_result;                /* 1 continue, 0 quit */
static int s_busy;                  /* extracting */
static int s_valid;                 /* 1 folder, 2 image */
static int s_err;                   /* s_msg is a failure */
static int s_hint;                  /* s_msg is the opening hint */
static char s_msg[512];
static uint32_t s_prog_done, s_prog_total;
static char s_iso_path[MAX_PATH];

static const COLORREF C_BG = RGB(8, 14, 8), C_PANEL = RGB(4, 22, 4), C_BAR = RGB(36, 150, 22),
                      C_TEXT = RGB(220, 235, 220), C_DIM = RGB(140, 170, 140),
                      C_EDIT = RGB(58, 64, 58), C_BTN = RGB(28, 92, 22), C_BTN_OFF = RGB(30, 42, 30),
                      C_OK = RGB(120, 230, 110), C_BAD = RGB(240, 120, 100);

static void get_text(HWND h, char *buf, int n)
{
    GetWindowTextA(h, buf, n);
}

static void text_at(HDC dc, int x, int y, const char *s)
{
    TextOutA(dc, x, y, s, (int)strlen(s));
}

static void validate(void)
{
    char g[MAX_PATH];
    get_text(s_game, g, MAX_PATH);
    s_valid = 0;
    s_err = 0;
    s_hint = !g[0];
    if (!g[0])
        snprintf(s_msg, sizeof s_msg, "Choose your MechAssault 2 game files: the folder with "
                 "ma2.xbe (an extracted disc), or the disc image.");
    else if (has_game(g)) {
        s_valid = 1;
        snprintf(s_msg, sizeof s_msg, "Found ma2.xbe. Ready.");
    } else if ((ends_with_ci(g, ".iso") || ends_with_ci(g, ".xiso")) && file_exists(g)) {
        s_valid = 2;
        snprintf(s_msg, sizeof s_msg, "Disc image. Continue extracts it once (about 2.2 GB) to:");
    } else if (dir_exists(g))
        snprintf(s_msg, sizeof s_msg, "No ma2.xbe in that folder.");
    else
        snprintf(s_msg, sizeof s_msg, "Not found.");
    EnableWindow(s_cont, s_valid && !s_busy);
    InvalidateRect(s_wnd, NULL, TRUE);
}

static int pick(HWND owner, int folder, char *out)
{
    IFileOpenDialog *d = NULL;
    IShellItem *it = NULL;
    PWSTR w = NULL;
    DWORD opts;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&d)))
        return 0;
    IFileOpenDialog_GetOptions(d, &opts);
    IFileOpenDialog_SetOptions(d, opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    if (!folder) {
        COMDLG_FILTERSPEC f[2] = { { L"Xbox disc image (*.iso, *.xiso)", L"*.iso;*.xiso" },
                                   { L"All files", L"*.*" } };
        IFileOpenDialog_SetFileTypes(d, 2, f);
        IFileOpenDialog_SetTitle(d, L"Choose the MechAssault 2 disc image");
    } else {
        IFileOpenDialog_SetTitle(d, L"Choose a folder");
    }
    if (SUCCEEDED(IFileOpenDialog_Show(d, owner)) && SUCCEEDED(IFileOpenDialog_GetResult(d, &it))
        && SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &w))) {
        WideCharToMultiByte(CP_ACP, 0, w, -1, out, MAX_PATH, NULL, NULL);
        CoTaskMemFree(w);
        ok = 1;
    }
    if (it) IShellItem_Release(it);
    IFileOpenDialog_Release(d);
    return ok;
}

static DWORD WINAPI extract_thread(void *arg)
{
    char err[512];
    int ok = xiso_extract(s_iso_path, (HWND)arg, err, sizeof err);
    static char msg[512];
    snprintf(msg, sizeof msg, "%s", err);
    PostMessageA((HWND)arg, WM_APP + 2, (WPARAM)ok, (LPARAM)msg);
    return 0;
}

static void draw_button(const DRAWITEMSTRUCT *di)
{
    char t[64];
    int on = !(di->itemState & ODS_DISABLED);
    HBRUSH b = CreateSolidBrush(on ? ((di->itemState & ODS_SELECTED) ? C_BAR : C_BTN) : C_BTN_OFF);
    FillRect(di->hDC, &di->rcItem, b);
    DeleteObject(b);
    GetWindowTextA(di->hwndItem, t, sizeof t);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, on ? C_TEXT : C_DIM);
    SelectObject(di->hDC, s_font);
    DrawTextA(di->hDC, t, -1, (RECT *)&di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* While an image extracts, nothing else can be pressed. */
static void set_busy(HWND h, int busy)
{
    static const int ids[] = { ID_GAME, ID_SAVES, ID_BROWSE_DIR, ID_BROWSE_ISO,
                               ID_BROWSE_SAVES, ID_CONTINUE, ID_QUIT };
    size_t i;
    s_busy = busy;
    for (i = 0; i < sizeof ids / sizeof ids[0]; i++)
        EnableWindow(GetDlgItem(h, ids[i]), !busy);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, C_EDIT);
        return (LRESULT)s_edit_bg;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, C_PANEL);
        return (LRESULT)s_panel_bg;
    case WM_DRAWITEM:
        draw_button((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc, panel = { 24, 24, 676, 312 }, bar = { 24, 24, 676, 50 };
        HBRUSH b;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, s_bg);
        b = CreateSolidBrush(C_PANEL); FillRect(dc, &panel, b); DeleteObject(b);
        b = CreateSolidBrush(C_BAR); FrameRect(dc, &panel, b); FillRect(dc, &bar, b); DeleteObject(b);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, s_font_title);
        SetTextColor(dc, RGB(240, 255, 240));
        text_at(dc, 36, 28, "MechAssault 2 - Game Path Setup");
        SelectObject(dc, s_font);
        SetTextColor(dc, C_DIM);
        text_at(dc, 36, 62, "Set where your game files are before the game starts.");
        SetTextColor(dc, C_TEXT);
        text_at(dc, 36, 98, "Game files");
        text_at(dc, 36, 152, "Saves (optional)");
        SetTextColor(dc, C_DIM);
        text_at(dc, 190, 176, "Empty: %LOCALAPPDATA%\\xboxrecomp, as before.");
        SetTextColor(dc, s_err ? C_BAD : (s_valid || s_busy) ? C_OK : s_hint ? C_DIM : C_BAD);
        {
            RECT t = { 36, 206, 664, 244 };
            DrawTextA(dc, s_msg, -1, &t, DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
        }
        if (s_valid == 2 && !s_err) {   /* where the image goes */
            char dst[MAX_PATH + 8];
            RECT t = { 36, 226, 664, 244 };
            snprintf(dst, sizeof dst, "%s\\game", s_base);
            SetTextColor(dc, C_DIM);
            DrawTextA(dc, dst, -1, &t, DT_SINGLELINE | DT_PATH_ELLIPSIS);
        }
        if (s_busy && s_prog_total) {
            RECT pb = { 36, 250, 664, 262 }, fill = pb;
            b = CreateSolidBrush(C_EDIT); FillRect(dc, &pb, b); DeleteObject(b);
            fill.right = pb.left + (LONG)((pb.right - pb.left) * (double)s_prog_done / s_prog_total);
            b = CreateSolidBrush(C_BAR); FillRect(dc, &fill, b); DeleteObject(b);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_GAME:
            if (HIWORD(wp) == EN_CHANGE)
                validate();
            break;
        case ID_BROWSE_DIR:
        case ID_BROWSE_ISO: {
            char p[MAX_PATH];
            if (pick(h, LOWORD(wp) == ID_BROWSE_DIR, p))
                SetWindowTextA(s_game, p);
            break;
        }
        case ID_BROWSE_SAVES: {
            char p[MAX_PATH];
            if (pick(h, 1, p))
                SetWindowTextA(s_saves, p);
            break;
        }
        case ID_CONTINUE:
            if (s_valid == 2) {
                get_text(s_game, s_iso_path, MAX_PATH);
                s_prog_done = 0; s_prog_total = 0;
                set_busy(h, 1);
                snprintf(s_msg, sizeof s_msg, "Extracting the disc image...");
                InvalidateRect(h, NULL, TRUE);
                CloseHandle(CreateThread(NULL, 0, extract_thread, h, 0, NULL));
            } else if (s_valid == 1) {
                char g[MAX_PATH], sv[MAX_PATH];
                get_text(s_game, g, MAX_PATH);
                get_text(s_saves, sv, MAX_PATH);
                settings_save(g, sv);
                s_result = 1;
                DestroyWindow(h);
            }
            break;
        case ID_QUIT:
            if (!s_busy)
                DestroyWindow(h);
            break;
        }
        return 0;
    case WM_APP + 1:
        s_prog_done = (uint32_t)wp;
        s_prog_total = (uint32_t)lp;
        snprintf(s_msg, sizeof s_msg, "Extracting the disc image... %u of %u MB", s_prog_done, s_prog_total);
        {
            RECT st = { 36, 206, 664, 264 };    /* the status lines and bar */
            InvalidateRect(h, &st, FALSE);
        }
        return 0;
    case WM_APP + 2:
        set_busy(h, 0);
        if (wp) {
            char g[MAX_PATH], sv[MAX_PATH];
            snprintf(g, sizeof g, "%s\\game", s_base);
            get_text(s_saves, sv, MAX_PATH);
            settings_save(g, sv);
            s_result = 1;
            DestroyWindow(h);
        } else {
            validate();             /* Continue tries again */
            snprintf(s_msg, sizeof s_msg, "%s", (const char *)lp);
            s_err = 1;
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case WM_CLOSE:
        if (!s_busy)
            DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

static HWND child(const char *cls, const char *text, DWORD style, int x, int y, int w, int hgt, int id)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, hgt, s_wnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)s_font, TRUE);
    return c;
}

static int run_window(const char *game, const char *saves)
{
    WNDCLASSA wc;
    MSG msg;
    RECT r = { 0, 0, 700, 336 };
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MA2Setup";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);
    s_bg = CreateSolidBrush(C_BG);
    s_edit_bg = CreateSolidBrush(C_EDIT);
    s_panel_bg = CreateSolidBrush(C_PANEL);
    s_font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    s_font_title = CreateFontA(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    s_wnd = CreateWindowExA(0, "MA2Setup", "MechAssault 2",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                            NULL, NULL, wc.hInstance, NULL);
    s_game = child("EDIT", game, ES_AUTOHSCROLL, 190, 94, 250, 24, ID_GAME);
    child("BUTTON", "Folder...", BS_OWNERDRAW, 448, 94, 92, 24, ID_BROWSE_DIR);
    child("BUTTON", "Disc image...", BS_OWNERDRAW, 548, 94, 118, 24, ID_BROWSE_ISO);
    s_saves = child("EDIT", saves, ES_AUTOHSCROLL, 190, 148, 350, 24, ID_SAVES);
    child("BUTTON", "Folder...", BS_OWNERDRAW, 548, 148, 118, 24, ID_BROWSE_SAVES);
    s_cont = child("BUTTON", "Continue", BS_OWNERDRAW, 36, 270, 140, 28, ID_CONTINUE);
    child("BUTTON", "Quit", BS_OWNERDRAW, 186, 270, 100, 28, ID_QUIT);
    validate();
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
    s_result = 0;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    DeleteObject(s_font); DeleteObject(s_font_title);
    DeleteObject(s_bg); DeleteObject(s_edit_bg); DeleteObject(s_panel_bg);
    if (SUCCEEDED(com))
        CoUninitialize();
    return s_result;
}

/* ---- entry ------------------------------------------------------------------- */

/* Called first thing in WinMain: the base folder, play.bat's environment and
 * the log, before anything prints or reads a setting. */
void ma2_EarlyInit(void)
{
    resolve_base();
    default_env();
    redirect_log();
}

/* Where the game files and the saves are; 0 when the player quit the setup
 * window. saves is "" for the default location. */
int ma2_SetupPaths(char *game, char *saves, const char *cmdline)
{
    char cfg_game[MAX_PATH], cfg_saves[MAX_PATH], def[MAX_PATH];
    const char *force = getenv("MA2_SETUP");
    const char *iso = getenv("MA2_SETUP_ISO");
    int ask = (force && *force == '1') || (cmdline && strstr(cmdline, "--setup"))
            || (GetAsyncKeyState(VK_SHIFT) & 0x8000);

    settings_load(cfg_game, cfg_saves);
    snprintf(saves, MAX_PATH, "%s", cfg_saves);
    if (iso && *iso) {              /* tests: extract without the window */
        char err[512];
        int ok = xiso_extract(iso, NULL, err, sizeof err);
        fprintf(stderr, "[SETUP] extract %s: %s\n", iso, ok ? "done" : err);
        if (!ok)
            return 0;
        snprintf(cfg_game, sizeof cfg_game, "%s\\game", s_base);
        settings_save(cfg_game, cfg_saves);
    }
    snprintf(def, sizeof def, "%s\\game", s_base);
    if (!ask && has_game(cfg_game)) {
        snprintf(game, MAX_PATH, "%s", cfg_game);
    } else if (!ask && !cfg_game[0] && has_game(def)) {
        snprintf(game, MAX_PATH, "%s", def);
    } else {
        if (!run_window(cfg_game[0] ? cfg_game : (has_game(def) ? def : ""), cfg_saves))
            return 0;
        settings_load(cfg_game, cfg_saves);
        if (!has_game(cfg_game))
            return 0;
        snprintf(game, MAX_PATH, "%s", cfg_game);
        snprintf(saves, MAX_PATH, "%s", cfg_saves);
    }
    fprintf(stderr, "[SETUP] game files %s; saves %s\n", game, saves[0] ? saves : "(default)");
    return 1;
}
