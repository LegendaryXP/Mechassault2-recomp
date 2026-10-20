/*
 * MechAssault 2: keyboard and mouse prompts.
 *
 * Playing with the mouse and keyboard (RECOMP_MKB), the menus' button icons
 * -- the green A by "Select", the red B by "Back", the blue X, the white
 * ball by "Player Options" -- show the key that presses that button
 * instead: a key cap with its name, or a mouse with the button lit. The key
 * is read from the layout (mkb.ini) through mkb_binding(), so a rebinding
 * shows up. Where a pad button has several keys the icon names the one a
 * menu wants: Enter, Backspace, Esc or Tab when one of them is bound, else
 * the first key in the file, a mouse button last.
 *
 * The icons are 32x32 DXT5 textures; the D3D11 back end hands every texture
 * it decodes to ma2_prompt_tex() by a hash of its bytes
 * (nv2a_backend_set_tex_replace), and this returns a 64x64 picture for the
 * ones in k_icons. RECOMP_TEX_HASHES=64 logs the hashes of the textures a
 * run shows and saves each one, to find more (the Y and Black icons have not
 * been seen yet).
 *
 * The pictures are drawn here, four times oversampled: shapes as coverage,
 * the label through GDI (white text on black, read back as a mask).
 *
 * MA2_PROMPTS=0 keeps the pad's icons.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int mkb_enabled(void);
const char *mkb_binding(const char *control, int n);
typedef int (*TexReplaceFn)(uint64_t hash, const void *tex,
                            const uint32_t **argb, uint32_t *w, uint32_t *h);
void nv2a_backend_set_tex_replace(TexReplaceFn fn);

#define ICON 64                 /* the picture: twice the pad icon's 32 */
#define SS   4                  /* oversampling */
#define BIG  (ICON * SS)

/* The pad icons, by the D3D11 back end's hash of their texture bytes. */
static const struct { uint64_t hash; const char *control; } k_icons[] = {
    { 0x3BA971964746E804ull, "A" },
    { 0xBE65022E45405E53ull, "B" },
    { 0xA70CDE254A37BBC3ull, "X" },
    { 0x16744E39E421F5D4ull, "WHITE" },
};
#define NICONS (sizeof k_icons / sizeof k_icons[0])

static uint32_t s_pic[NICONS][ICON * ICON];
static int s_made[NICONS];       /* 1 drawn, -1 nothing bound */

/* ---- choosing the key ----------------------------------------------------- */

static int is_mouse(const char *k)
{
    return !strncmp(k, "MOUSE", 5) || !strncmp(k, "WHEEL", 5);
}

/* The key an icon shows for a pad control (see the top of the file). */
static const char *icon_key(const char *control)
{
    static const char *menu[] = { "ENTER", "BACKSPACE", "ESC", "TAB" };
    const char *k, *best = NULL;
    int n, i, score, best_score = 99;
    for (n = 0; (k = mkb_binding(control, n)) != NULL; n++) {
        score = is_mouse(k) ? 9 : 8;
        for (i = 0; i < 4; i++)
            if (!strcmp(k, menu[i]))
                score = i;
        if (score < best_score) {
            best_score = score;
            best = k;
        }
    }
    return best;
}

/* What a key cap says for a layout key name. */
static const wchar_t *cap_label(const char *k, wchar_t *buf, size_t n)
{
    static const struct { const char *name; const wchar_t *label; } names[] = {
        {"ENTER", L"Enter"}, {"BACKSPACE", L"Bksp"}, {"ESC", L"Esc"}, {"TAB", L"Tab"},
        {"SPACE", L"Space"}, {"CAPSLOCK", L"Caps"}, {"LSHIFT", L"Shift"},
        {"RSHIFT", L"Shift"}, {"LCTRL", L"Ctrl"}, {"RCTRL", L"Ctrl"}, {"LALT", L"Alt"},
        {"RALT", L"Alt"}, {"UP", L"\x2191"}, {"DOWN", L"\x2193"}, {"LEFT", L"\x2190"},
        {"RIGHT", L"\x2192"}, {"INSERT", L"Ins"}, {"DELETE", L"Del"}, {"HOME", L"Home"},
        {"END", L"End"}, {"PGUP", L"PgUp"}, {"PGDN", L"PgDn"}, {"COMMA", L","},
        {"PERIOD", L"."}, {"SLASH", L"/"}, {"SEMICOLON", L";"}, {"QUOTE", L"'"},
        {"LBRACKET", L"["}, {"RBRACKET", L"]"}, {"BACKSLASH", L"\\"}, {"MINUS", L"-"},
        {"EQUALS", L"="}, {"GRAVE", L"`"}, {"MOUSE4", L"M4"}, {"MOUSE5", L"M5"},
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(k, names[i].name))
            return names[i].label;
    if (!strncmp(k, "NUM", 3) && k[3]) {
        _snwprintf(buf, n, L"Num%hs", k + 3);
        return buf;
    }
    _snwprintf(buf, n, L"%hs", k);         /* letters, digits, F1-F12 */
    return buf;
}

/* ---- drawing -------------------------------------------------------------- */

static float s_rgba[BIG * BIG][4];       /* straight colour, alpha */

static void put(int i, float r, float g, float b, float a)
{
    float *p = s_rgba[i], oa = p[3], na = a + oa * (1.0f - a);
    if (na <= 0.0f)
        return;
    p[0] = (r * a + p[0] * oa * (1.0f - a)) / na;
    p[1] = (g * a + p[1] * oa * (1.0f - a)) / na;
    p[2] = (b * a + p[2] * oa * (1.0f - a)) / na;
    p[3] = na;
}

/* Inside a rounded rectangle (picture units, ICON across). */
static int in_rrect(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    float cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x;
    float cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;
    if (x < x0 || x > x1 || y < y0 || y > y1)
        return 0;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

/* Fill a rounded rectangle; the colour runs from top to bottom (rgb0 to
 * rgb1). */
static void rrect(float x0, float y0, float x1, float y1, float r,
                  uint32_t top, uint32_t bottom, float a)
{
    int px, py;
    for (py = 0; py < BIG; py++)
        for (px = 0; px < BIG; px++) {
            float x = (px + 0.5f) / SS, y = (py + 0.5f) / SS, t;
            if (!in_rrect(x, y, x0, y0, x1, y1, r))
                continue;
            t = (y - y0) / (y1 - y0 > 0 ? y1 - y0 : 1);
            put(py * BIG + px,
                (((top >> 16) & 0xFF) * (1 - t) + ((bottom >> 16) & 0xFF) * t) / 255.0f,
                (((top >> 8) & 0xFF) * (1 - t) + ((bottom >> 8) & 0xFF) * t) / 255.0f,
                ((top & 0xFF) * (1 - t) + (bottom & 0xFF) * t) / 255.0f, a);
        }
}

/* A label in `rgb`, centred on (cx, cy), at most `maxw` wide and `maxh`
 * tall (picture units). */
static void label(const wchar_t *s, float cx, float cy, float maxw, float maxh, uint32_t rgb)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm, oldb;
    HFONT f = NULL, oldf = NULL;
    SIZE sz;
    RECT rc;
    int h = (int)(maxh * SS), i, len = (int)wcslen(s);

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = BIG;
    bi.bmiHeader.biHeight = -BIG;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dc || !bm || !bits) {
        if (bm) DeleteObject(bm);
        if (dc) DeleteDC(dc);
        return;
    }
    oldb = (HBITMAP)SelectObject(dc, bm);
    memset(bits, 0, (size_t)BIG * BIG * 4);
    /* The largest bold Arial that fits. */
    for (; h > 8; h -= h / 12 + 1) {
        if (f) {
            SelectObject(dc, oldf);
            DeleteObject(f);
        }
        f = CreateFontW(-h, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, L"Arial");
        oldf = (HFONT)SelectObject(dc, f);
        if (GetTextExtentPoint32W(dc, s, len, &sz) && sz.cx <= maxw * SS)
            break;
    }
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    rc.left = (LONG)((cx - maxw / 2) * SS);
    rc.right = (LONG)((cx + maxw / 2) * SS);
    rc.top = (LONG)((cy - maxh / 2) * SS);
    rc.bottom = (LONG)((cy + maxh / 2) * SS);
    DrawTextW(dc, s, len, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    GdiFlush();
    for (i = 0; i < BIG * BIG; i++) {
        float a = ((const uint8_t *)bits)[i * 4 + 1] / 255.0f;
        if (a > 0.0f)
            put(i, ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                (rgb & 0xFF) / 255.0f, a);
    }
    if (f) {
        SelectObject(dc, oldf);
        DeleteObject(f);
    }
    SelectObject(dc, oldb);
    DeleteObject(bm);
    DeleteDC(dc);
}

/* A key cap: shadow, dark rim, light face, the key's name. */
static void draw_key(const wchar_t *text)
{
    int wide = wcslen(text) > 1;
    rrect(3, 6, 61, 63, 11, 0x000000, 0x000000, 0.45f);
    rrect(2, 2, 62, 60, 11, 0x262626, 0x0E0E0E, 1.0f);
    rrect(5, 5, 59, 54, 8, 0xFBFBFB, 0xC9C9C9, 1.0f);
    label(text, 32, 29.5f, 53, wide ? 30 : 42, 0x141414);
}

/* A mouse with one part lit: 0 left, 1 right, 2 middle (wheel), 3 wheel up,
 * 4 wheel down. */
static void draw_mouse(int part)
{
    const uint32_t lit = 0xFF9A1A, lit2 = 0xE07000;
    int px, py;
    rrect(14, 5, 52, 63, 17, 0x000000, 0x000000, 0.45f);
    rrect(12, 2, 50, 60, 17, 0x262626, 0x0E0E0E, 1.0f);
    rrect(15, 5, 47, 57, 14, 0xFBFBFB, 0xC9C9C9, 1.0f);
    /* The lit button: the top half's left or right side. */
    for (py = 0; py < BIG; py++)
        for (px = 0; px < BIG; px++) {
            float x = (px + 0.5f) / SS, y = (py + 0.5f) / SS, t = (y - 5) / 22;
            if (part > 1 || y > 27 || !in_rrect(x, y, 15, 5, 47, 57, 14))
                continue;
            if ((part == 0 && x < 31) || (part == 1 && x > 31))
                put(py * BIG + px,
                    (((lit >> 16) & 0xFF) * (1 - t) + ((lit2 >> 16) & 0xFF) * t) / 255.0f,
                    (((lit >> 8) & 0xFF) * (1 - t) + ((lit2 >> 8) & 0xFF) * t) / 255.0f,
                    ((lit & 0xFF) * (1 - t) + (lit2 & 0xFF) * t) / 255.0f, 1.0f);
        }
    /* The seams, and the wheel. */
    rrect(30.2f, 5, 31.8f, 27, 0.5f, 0x262626, 0x262626, 1.0f);
    rrect(15, 26.2f, 47, 27.8f, 0.5f, 0x262626, 0x262626, 1.0f);
    rrect(27, 10, 35, 23, 4, 0x262626, 0x262626, 1.0f);
    rrect(28.5f, 11.5f, 33.5f, 21.5f, 2.5f, part >= 2 ? lit : 0x8A8A8A,
          part >= 2 ? lit2 : 0x6A6A6A, 1.0f);
    if (part == 3)
        label(L"\x25B2", 31, 42, 20, 16, 0x262626);
    else if (part == 4)
        label(L"\x25BC", 31, 42, 20, 16, 0x262626);
}

static void make(int n)
{
    const char *k = icon_key(k_icons[n].control);
    wchar_t buf[16];
    int x, y, sx, sy;

    if (!k) {
        s_made[n] = -1;
        return;
    }
    memset(s_rgba, 0, sizeof s_rgba);
    if (!strcmp(k, "MOUSE1"))
        draw_mouse(0);
    else if (!strcmp(k, "MOUSE2"))
        draw_mouse(1);
    else if (!strcmp(k, "MOUSE3"))
        draw_mouse(2);
    else if (!strcmp(k, "WHEELUP"))
        draw_mouse(3);
    else if (!strcmp(k, "WHEELDOWN"))
        draw_mouse(4);
    else
        draw_key(cap_label(k, buf, 16));
    /* Down to ICON x ICON, alpha-weighted. */
    for (y = 0; y < ICON; y++)
        for (x = 0; x < ICON; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (sy = 0; sy < SS; sy++)
                for (sx = 0; sx < SS; sx++) {
                    const float *p = s_rgba[(y * SS + sy) * BIG + x * SS + sx];
                    r += p[0] * p[3];
                    g += p[1] * p[3];
                    b += p[2] * p[3];
                    a += p[3];
                }
            if (a > 0.0f) {
                r /= a; g /= a; b /= a;
            }
            a /= SS * SS;
            s_pic[n][y * ICON + x] = ((uint32_t)(a * 255.0f + 0.5f) << 24)
                | ((uint32_t)(r * 255.0f + 0.5f) << 16)
                | ((uint32_t)(g * 255.0f + 0.5f) << 8) | (uint32_t)(b * 255.0f + 0.5f);
        }
    s_made[n] = 1;
    fprintf(stderr, "[PROMPT] %s icon shows %s\n", k_icons[n].control, k);
}

static int ma2_prompt_tex(uint64_t hash, const void *tex, const uint32_t **argb,
                          uint32_t *w, uint32_t *h)
{
    size_t n;
    (void)tex;
    for (n = 0; n < NICONS; n++)
        if (k_icons[n].hash == hash) {
            if (!s_made[n])
                make((int)n);
            if (s_made[n] < 0)
                return 0;
            *argb = s_pic[n];
            *w = *h = ICON;
            return 1;
        }
    return 0;
}

/* ---- tutorial and hint text ------------------------------------------------ *
 *
 * The game's text names the pad: "Pull the Right trigger to fire lasers.",
 * "Press A to open the gate.", "Press White button to review tutorial
 * text." (Text.mgf, UTF-16, read in 16 KB pieces into the game's heap). A
 * thread looks through guest memory every two seconds for such strings and
 * rewrites them in place to name the keys: "Left-click to fire lasers.",
 * "Press E to open the gate.", "Press Tab to review tutorial text." A string
 * keeps its length -- the rest is padded with spaces -- so one whose
 * rewrite would be longer is left alone (logged). English only.
 *
 * The key named is the first keyboard key bound to the control in mkb.ini
 * (the mouse when no key is), so "Press Q" for the Left trigger, which the
 * wheel also works.
 */
extern ptrdiff_t g_xbox_mem_offset;

static int is_kbd(const char *k) { return k && !is_mouse(k); }

static const char *text_key(const char *control)
{
    const char *k, *first = NULL;
    int n;
    for (n = 0; (k = mkb_binding(control, n)) != NULL; n++) {
        if (!first)
            first = k;
        if (is_kbd(k))
            return k;
    }
    return first;
}

/* A key's name in a sentence. */
static void key_word(const char *k, wchar_t *out, size_t n)
{
    static const struct { const char *name; const wchar_t *word; } words[] = {
        {"ENTER", L"Enter"}, {"BACKSPACE", L"Backspace"}, {"ESC", L"Esc"}, {"TAB", L"Tab"},
        {"SPACE", L"Space"}, {"CAPSLOCK", L"Caps Lock"}, {"LSHIFT", L"Shift"},
        {"RSHIFT", L"Shift"}, {"LCTRL", L"Ctrl"}, {"RCTRL", L"Ctrl"}, {"LALT", L"Alt"},
        {"RALT", L"Alt"}, {"UP", L"Up"}, {"DOWN", L"Down"}, {"LEFT", L"Left"},
        {"RIGHT", L"Right"},
    };
    wchar_t buf[16];
    size_t i;
    for (i = 0; i < sizeof words / sizeof words[0]; i++)
        if (!strcmp(k, words[i].name)) {
            _snwprintf(out, n, L"%ls", words[i].word);
            return;
        }
    _snwprintf(out, n, L"%ls", cap_label(k, buf, 16));
}

/* "{V:RT}" -> "Left-click" / "Press E"; "{N:RT}" -> "Left mouse" / "E".
 * Upper-case V/N capitalise the first letter. Returns 0 when nothing is
 * bound. */
static int phrase(char kind, const char *control, wchar_t *out, size_t n)
{
    const char *k = text_key(control);
    wchar_t w[24];
    int verb = kind == 'V' || kind == 'v', cap = kind == 'V' || kind == 'N';
    if (!k)
        return 0;
    if (!strcmp(k, "MOUSE1"))
        _snwprintf(out, n, verb ? L"left-click" : L"left mouse");
    else if (!strcmp(k, "MOUSE2"))
        _snwprintf(out, n, verb ? L"right-click" : L"right mouse");
    else if (!strcmp(k, "MOUSE3"))
        _snwprintf(out, n, verb ? L"middle-click" : L"middle mouse");
    else if (!strncmp(k, "WHEEL", 5))
        _snwprintf(out, n, verb ? L"scroll" : L"the mouse wheel");
    else if (!strcmp(k, "MOUSE4") || !strcmp(k, "MOUSE5"))
        _snwprintf(out, n, verb ? L"press mouse %c" : L"mouse %c", k[5]);
    else {
        key_word(k, w, 24);
        _snwprintf(out, n, verb ? L"press %ls" : L"%ls", w);
    }
    out[n - 1] = 0;
    if (cap && out[0] >= L'a' && out[0] <= L'z')
        out[0] = (wchar_t)(out[0] - 32);
    return 1;
}

/* Whole phrases, longest first. {V:..} {v:..} {N:..} {n:..} as phrase(). */
static const struct { const wchar_t *from; const char *to; } k_rules[] = {
    { L"Move the Right thumbstick up and down", "Move the mouse up and down" },
    { L"Use the Right thumbstick to aim", "Use the mouse to aim" },
    { L"Press left and right on the D-pad", "Press {n:DPAD_LEFT} and {n:DPAD_RIGHT}" },
    { L"Press up and down on the D-pad", "Press {n:DPAD_UP} and {n:DPAD_DOWN}" },
    { L"Move the Left thumbstick down", "{V:MOVE_BACK}" },
    { L"Move the Left thumbstick up", "{V:MOVE_FORWARD}" },
    { L"move the Left thumbstick up", "{v:MOVE_FORWARD}" },
    { L"hold down the Right trigger", "hold {n:RT}" },
    { L"Click the Right thumbstick", "{V:RTHUMB}" },
    { L"click the Right thumbstick", "{v:RTHUMB}" },
    { L"Release the Right trigger", "Release {n:RT}" },
    { L"The Right trigger fires", "{N:RT} fires" },
    { L"Pull the Right trigger", "{V:RT}" },
    { L"pull the Right trigger", "{v:RT}" },
    { L"Pull the Left trigger", "{V:LT}" },
    { L"Use the Left trigger", "Use {n:LT}" },
    { L"Click Left thumbstick", "{V:LTHUMB}" },
    { L"pressing the B button", "pressing {n:B}" },
    { L"Press White button", "{V:WHITE}" },
    { L"Pull Left trigger", "{V:LT}" },
    { L"Press any button", "Press any key" },
    { L"Press START", "{V:START}" },
    { L"Press Start", "{V:START}" },
    { L" or B to ", " or {n:B} to " },
};

/* Expand a rule's template; 0 if a control in it has no key. */
static int expand(const char *t, wchar_t *out, size_t n)
{
    size_t o = 0;
    while (*t && o + 1 < n) {
        if (t[0] == '{' && t[2] == ':') {
            char ctl[24];
            const char *e = strchr(t, '}');
            wchar_t w[48];
            size_t l = (size_t)(e - t - 3), i;
            if (!e || l >= sizeof ctl)
                return 0;
            memcpy(ctl, t + 3, l);
            ctl[l] = 0;
            if (!phrase(t[1], ctl, w, 48))
                return 0;
            for (i = 0; w[i] && o + 1 < n; i++)
                out[o++] = w[i];
            t = e + 1;
        } else {
            out[o++] = (wchar_t)(unsigned char)*t++;
        }
    }
    out[o] = 0;
    return 1;
}

static int is_letter(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

/* Rewrite one string (in: s, its length len) into out; 1 if it changed. */
static int rewrite(const wchar_t *s, size_t len, wchar_t *out, size_t cap)
{
    wchar_t a[1100], b[1100], *cur = a, *nxt = b, w[64];
    size_t r, i, o, fl, wl;
    int changed = 0;

    if (len >= 1024)
        return 0;
    memcpy(a, s, len * sizeof(wchar_t));
    a[len] = 0;
    for (r = 0; r < sizeof k_rules / sizeof k_rules[0]; r++) {
        fl = wcslen(k_rules[r].from);
        if (!wcsstr(cur, k_rules[r].from) || !expand(k_rules[r].to, w, 64))
            continue;
        wl = wcslen(w);
        for (i = o = 0; cur[i] && o + wl + 1 < 1100;) {
            if (!wcsncmp(cur + i, k_rules[r].from, fl)) {
                memcpy(nxt + o, w, wl * sizeof(wchar_t));
                o += wl;
                i += fl;
                changed = 1;
            } else {
                nxt[o++] = cur[i++];
            }
        }
        nxt[o] = 0;
        { wchar_t *t = cur; cur = nxt; nxt = t; }
    }
    /* "Press A", "press Y", "Press  X  X  B  Y  Y  X  A  to hack": the face
     * buttons after "Press", one letter each. */
    for (i = o = 0; cur[i] && o + 40 < 1100;) {
        int at_press = (cur[i] == L'P' || cur[i] == L'p') && !wcsncmp(cur + i + 1, L"ress ", 5)
                       && (i == 0 || !is_letter(cur[i - 1]));
        if (!at_press) {
            nxt[o++] = cur[i++];
            continue;
        }
        memcpy(nxt + o, cur + i, 6 * sizeof(wchar_t));
        o += 6;
        i += 6;
        for (;;) {
            size_t j = i;
            const char *ctl = NULL;
            while (cur[j] == L' ')
                j++;
            switch (cur[j]) {
            case L'A': ctl = "A"; break;
            case L'B': ctl = "B"; break;
            case L'X': ctl = "X"; break;
            case L'Y': ctl = "Y"; break;
            }
            if (!ctl || is_letter(cur[j + 1]) || !phrase('n', ctl, w, 64))
                break;
            while (i < j)
                nxt[o++] = cur[i++];
            for (wl = 0; w[wl] && o + 1 < 1100; wl++)
                nxt[o++] = w[wl];
            i = j + 1;
            changed = 1;
        }
    }
    nxt[o] = 0;
    cur = nxt;
    if (!changed || wcslen(cur) + 1 > cap)
        return 0;
    wcscpy(out, cur);
    return 1;
}

/* Strings already rewritten, by address, with a hash of what they hold now:
 * a key named like a pad button (X bound to Y) must not be rewritten again. */
#define SEEN_MAX 8192
static struct { uint32_t va; uint32_t hash; } s_seen[SEEN_MAX];

static uint32_t whash(const wchar_t *s, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ s[i]) * 16777619u;
    return h;
}

static int seen_slot(uint32_t va)
{
    uint32_t i = (va >> 1) * 2654435761u % SEEN_MAX, k;
    for (k = 0; k < SEEN_MAX; k++, i = (i + 1) % SEEN_MAX)
        if (s_seen[i].va == va || s_seen[i].va == 0)
            return (int)i;
    return -1;
}

static int s_rewrites, s_too_long;

/* A candidate hit at p (2-byte aligned) in [lo, hi): find the string around
 * it and rewrite it if it names the pad. */
static void consider(wchar_t *lo, wchar_t *hi, wchar_t *p)
{
    wchar_t *st = p, *en = p, out[1100];
    size_t len, i;
    uint32_t va, h;
    int slot;

    while (st > lo && p - st < 600) {
        wchar_t c = st[-1];
        if (!((c >= 0x20 && c < 0x7F) || c == 0x0A || c == 0x0D || (c >= 0xA0 && c < 0x100)))
            break;
        st--;
    }
    if (st == lo || st[-1] != 0)
        return;                         /* not a whole string */
    while (en < hi && *en && en - st < 1024)
        en++;
    if (en >= hi || *en)
        return;
    len = (size_t)(en - st);
    va = (uint32_t)((uint8_t *)st - (uint8_t *)g_xbox_mem_offset);
    h = whash(st, len);
    slot = seen_slot(va);
    if (slot >= 0 && s_seen[slot].va == va && s_seen[slot].hash == h)
        return;
    if (rewrite(st, len, out, sizeof out / sizeof out[0])) {
        size_t nl = wcslen(out);
        if (nl > len) {
            if (s_too_long++ < 20)
                fprintf(stderr, "[PROMPT] text at %08X: \"%ls\" does not fit\n", va, out);
        } else {
            for (i = 0; i < len; i++)
                st[i] = i < nl ? out[i] : L' ';
            if (s_rewrites++ < 40)
                fprintf(stderr, "[PROMPT] text at %08X: \"%ls\"\n", va, out);
            h = whash(st, len);
        }
    }
    if (slot >= 0) {
        s_seen[slot].va = va;
        s_seen[slot].hash = h;
    }
}

/* The words that mark a candidate, as UTF-16 code units. */
static const wchar_t *k_needles[] = { L"trigger", L"thumbstick", L"D-pad", L"button", L"ress " };

static void scan_range(uint8_t *base, size_t size)
{
    wchar_t *lo = (wchar_t *)base, *hi = (wchar_t *)(base + (size & ~(size_t)1)), *p;
    size_t k;
    for (p = lo + 1; p + 12 < hi; p++) {
        wchar_t c = *p;
        if (c != L't' && c != L'D' && c != L'b' && c != L'r')
            continue;
        for (k = 0; k < sizeof k_needles / sizeof k_needles[0]; k++)
            if (c == k_needles[k][0] && !wcsncmp(p, k_needles[k], wcslen(k_needles[k]))) {
                consider(lo, hi, p);
                break;
            }
    }
}

static void scan_guest(void)
{
    uint8_t *base = (uint8_t *)g_xbox_mem_offset, *at = base, *end = base + 0x04000000;
    MEMORY_BASIC_INFORMATION mbi;
    while (at < end && VirtualQuery(at, &mbi, sizeof mbi)) {
        uint8_t *rs = (uint8_t *)mbi.BaseAddress, *re = rs + mbi.RegionSize;
        if (re > end)
            re = end;
        if (mbi.State == MEM_COMMIT
            && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))
            && !(mbi.Protect & PAGE_GUARD)) {
            __try {
                scan_range(rs < at ? at : rs, (size_t)(re - (rs < at ? at : rs)));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        at = re;
    }
}

static DWORD WINAPI text_thread(void *arg)
{
    int pass = 0;
    (void)arg;
    for (;;) {
        ULONGLONG t0;
        Sleep(2000);
        t0 = GetTickCount64();
        scan_guest();
        if (pass++ < 3 || (s_rewrites && pass % 150 == 0))
            fprintf(stderr, "[PROMPT] text scan %d: %llu ms, %d rewritten so far\n", pass,
                    (unsigned long long)(GetTickCount64() - t0), s_rewrites);
    }
    return 0;
}

/* ---- NeuroHack and bucking ------------------------------------------------- *
 *
 * The minigame lights buttons on the pad's face diamond (Y top, X left, B
 * right, A bottom) to press fast, sometimes two at once -- E, Z, F and G on
 * the normal layout, nowhere near each other. While the HUD shows the
 * diamond (recomp_manual.c's sub_0018EC9D hook, every frame) the layout's
 * "hack" mode is on: mkb.ini binds W A S D (and the arrows) to the button in
 * the same place, and Move stops for as long. It lapses 250 ms after the HUD
 * stops asking.
 */
void mkb_set_mode(const char *mode, int ms);

void ma2_hack_hud_seen(uint32_t session, uint32_t bit, uint32_t state)
{
    static uint32_t last_session;
    static int lines;
    mkb_set_mode("hack", 250);
    if (session != last_session) {
        last_session = session;
        lines = 0;
        fprintf(stderr, "[HACK] diamond on the HUD (session %08X)\n", session);
    }
    if (lines < 24 && state) {
        lines++;
        fprintf(stderr, "[HACK] button bit %02X state %u\n", bit, state);
    }
}

void ma2_PromptsStart(void)
{
    const char *e = getenv("MA2_PROMPTS");
    if (!mkb_enabled() || (e && *e == '0'))
        return;
    nv2a_backend_set_tex_replace(ma2_prompt_tex);
    CloseHandle(CreateThread(NULL, 0, text_thread, NULL, 0, NULL));
}
