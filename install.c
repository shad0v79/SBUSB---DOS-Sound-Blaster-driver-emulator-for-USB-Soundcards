/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * INSTALL.COM - instalator SBUSB (SBEMU + sterownik karty USB) dla FreeDOS,
 * MS-DOS 6.22 i MS-DOS 7.1. Licencja: MIT.
 *
 *   INSTALL            instalacja do <dysk startowy>:\SBUSB, z pytaniem
 *   INSTALL /D:C:\SB   inny katalog
 *   INSTALL /Y         bez pytan
 *   INSTALL /N         tylko kopiowanie plikow, bez zmian w CONFIG/AUTOEXEC
 *   INSTALL /U         odinstalowanie: przywraca kopie CONFIG/AUTOEXEC (*.SBU)
 *   INSTALL /MS        (test) traktuj system jak MS-DOS
 *
 * Do menu startowego dodawane sa dwie pozycje: SB16 przez USB (XMS) i (XMS + EMS).
 * Linie menedzerow pamieci z dotychczasowej konfiguracji (HIMEM, EMM386, JEMM,
 * QEMM, UMBPCI, DOS=) laduja sie tylko w dotychczasowej pozycji - w nowych
 * zastepuje je JEMMEX. Pozostale linie (CD-ROM, mysz...) laduja sie zawsze.
 * Przed zmiana powstaja kopie: CONFIG.SBU / FDCONFIG.SBU / AUTOEXEC.SBU.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end

void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
static void scat(char *d, const char *s) { scpy(d + slen(d), s); }
static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static int sieq_n(const char *a, const char *b, int n) { int i; for (i = 0; i < n; i++) if (up(a[i]) != up(b[i])) return 0; return 1; }
static int contains(const char *s, int n, const char *w)
{ int l = slen(w), i; for (i = 0; i + l <= n; i++) if (sieq_n(s + i, w, l)) return 1; return 0; }
static inline void outl(u16 p, u32 v) { asm volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 v; asm volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static u8 peekb(u16 seg, u16 off) { u8 v; asm volatile("movw %1,%%fs\n\tmovb %%fs:(%2),%0" : "=q"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }

/* DOS */
static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_read(u16 h, void *buf, u16 len)
{ u32 ax = 0x3F00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_open(const char *n) { u32 ax = 0x3D00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_create(const char *n) { u32 ax = 0x3C00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static int dos_mkdir(const char *n) { u32 ax = 0x3900; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "d"(n) : "memory", "cc"); return !cf; }
static int dos_exists(const char *n) { u32 ax = 0x4300, cx = 0; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf), "+c"(cx) : "d"(n) : "memory", "cc"); return !cf; }
static int dos_attr0(const char *n) { u32 ax = 0x4301, cx = 0; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf), "+c"(cx) : "d"(n) : "memory", "cc"); return !cf; }
static u16 getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }

static int fmt(char *o, int max, const char *f, va_list ap)
{
    int n = 0;
    while (*f) {
        char c = *f++;
        if (c != '%') { if (n < max - 1) o[n++] = c; continue; }
        c = *f++;
        if (c == 's') { const char *s = va_arg(ap, const char *); while (*s && n < max - 1) o[n++] = *s++; }
        else if (c == 'c') { o[n++] = (char)va_arg(ap, int); }
        else if (c == 'u') { u32 v = va_arg(ap, u32); char t[12]; int l = 0; do { t[l++] = (char)('0' + v % 10); v /= 10; } while (v); while (l && n < max - 1) o[n++] = t[--l]; }
        else if (n < max - 1) o[n++] = c;
    }
    o[n] = 0; return n;
}
static void pr(const char *f, ...)
{
    char b[400], o[800]; va_list ap; int n, i, m = 0;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    for (i = 0; i < n; i++) { if (b[i] == '\n') o[m++] = '\r'; o[m++] = b[i]; }
    dos_write(1, o, (u16)m);
}
static void sf(char *o, int max, const char *f, ...) { va_list ap; va_start(ap, f); fmt(o, max, f, ap); va_end(ap); }


#ifdef LANG_EN
#define L(pl, en) en
#else
#define L(pl, en) pl
#endif

/* ------------------------------------------------------------------ */
static char srcdir[80], dst[80], bootdrv = 'C';
static int is_fd, opt_y, opt_n, opt_u, opt_ms, is_ebox, full = -1;
static u8 cbuf[10240];   /* kopiowanie plikow i kopie zapasowe / file copy and backups */

static const char *const files[] = {
    "SBEMU.EXE", "HDPMI32I.EXE", "JEMMEX.EXE", "JLOAD.EXE", "QPIEMU.DLL", "HIMEMX.EXE",
    "USBMIX.COM", "USBREC.COM", "SBMIX.COM", "SBTEST.COM", "USBTONE.COM",
    "PAMIEC.COM", "STRONA.COM", "CPUTEST.COM", L("CZYTAJ.TXT", "README.TXT"), L("INSTALUJ.TXT", "INSTALL.TXT"),
    L("LICENCJA.TXT", "LICENSE.TXT"), "GPL2.TXT", 0
};

/* pozycje menu / menu entries: drv 0 = JEMMEX NOEMS, 1 = JEMMEX + EMS, 2 = HIMEMX (bez SBEMU) */
typedef struct { const char *label, *block, *mode; int drv; } entry_t;
static const entry_t entries[5] = {
    { L("SB16 przez USB: gry, pamiec XMS",              "SB16 over USB: games, XMS memory"),            "SBUSB",    "",     0 },
    { L("SB16 przez USB: gry, pamiec XMS + EMS",        "SB16 over USB: games, XMS + EMS memory"),      "SBUSBEMS", "",     1 },
    { L("SB16 przez USB: test karty, nagrywanie, mikser", "SB16 over USB: card test, recording, mixer"), "SBTEST",   "TEST", 0 },
    { L("Karta USB bez SBEMU: USBTONE, CPUTEST",        "USB card without SBEMU: USBTONE, CPUTEST"),    "SBBEZ",    "BEZ",  2 },
    { L("SB16 przez USB: diagnostyka (SBEMU recznie)",  "SB16 over USB: diagnostics (manual SBEMU)"),   "SBDIAG",   "DIAG", 0 },
};
#define PREV_LABEL L("Dotychczasowa konfiguracja", "Previous configuration")
static int nent;
static char ids[5][12];

static int copy_file(const char *name)
{
    char a[100], b[100]; int hs, hd, n;
    sf(a, sizeof a, "%s%s", srcdir, name); sf(b, sizeof b, "%s\\%s", dst, name);
    hs = dos_open(a);
    if (hs < 0) { pr(L("  BRAK: %s\n", "  MISSING: %s\n"), a); return 0; }
    hd = dos_create(b);
    if (hd < 0) { dos_close(hs); pr(L("  BLAD zapisu: %s\n", "  write error: %s\n"), b); return 0; }
    while ((n = dos_read((u16)hs, cbuf, sizeof cbuf)) > 0)
        if (dos_write((u16)hd, cbuf, (u16)n) != n) { dos_close(hs); dos_close(hd); pr(L("  BLAD zapisu (pelny dysk?): %s\n", "  write error (disk full?): %s\n"), b); return 0; }
    dos_close(hs); dos_close(hd);
    return 1;
}
static int write_text(const char *name, const char *text)
{
    int h = dos_create(name), n = slen(text);
    if (h < 0) return 0;
    if (dos_write((u16)h, text, (u16)n) != n) { dos_close(h); return 0; }
    dos_close(h); return 1;
}
static int read_text(const char *name, char *buf, int max)
{
    int h = dos_open(name), n;
    if (h < 0) return -1;
    n = dos_read((u16)h, buf, (u16)(max - 1)); dos_close(h);
    if (n < 0) return -1;
    while (n > 0 && (buf[n - 1] == 26 || buf[n - 1] == 0)) n--;
    buf[n] = 0; return n;
}
static int backup(const char *name, const char *bak)
{   /* kopia tylko raz - zostaje oryginal sprzed pierwszej instalacji */
    int n;
    if (dos_exists(bak)) return 1;
    n = read_text(name, (char *)cbuf, sizeof cbuf);
    if (n < 0) return 1;
    return write_text(bak, (const char *)cbuf);
}

/* ------------------------------------------------------------------ */
/* konfiguracja                                                         */
/* ------------------------------------------------------------------ */
static char cfg[10240], out[14336], aut[6144];
static int on;
static void emit(const char *s) { int l = slen(s); if (on + l < (int)sizeof out - 1) { memcpy(out + on, s, (unsigned)l); on += l; out[on] = 0; } }
static void emitn(const char *s, int n) { if (on + n < (int)sizeof out - 1) { memcpy(out + on, s, (unsigned)n); on += n; out[on] = 0; } }

/* linia -> poczatek i dlugosc (bez CR/LF), zwraca nastepna pozycje */
static int line_at(const char *b, int p, int *len) { int q = p; while (b[q] && b[q] != '\r' && b[q] != '\n') q++; *len = q - p; if (b[q] == '\r') q++; if (b[q] == '\n') q++; return q; }
static int skip_ws(const char *s, int n) { int i = 0; while (i < n && (s[i] == ' ' || s[i] == '\t')) i++; return i; }
/* FreeDOS: prefiks "123?" */
static int fd_prefix(const char *s, int n) { int i = 0; while (i < n && s[i] >= '0' && s[i] <= '9') i++; return (i > 0 && i < n && s[i] == '?') ? i + 1 : 0; }

static int is_memline(const char *s, int n)
{
    int i = skip_ws(s, n), p = fd_prefix(s + i, n - i);
    const char *t = s + i + p; int m = n - i - p;
    if (m >= 4 && sieq_n(t, "DOS=", 4)) return 1;
    if (!(m >= 6 && (sieq_n(t, "DEVICE", 6) || sieq_n(t, "INSTALL", 7)))) return 0;
    return contains(t, m, "HIMEM") || contains(t, m, "EMM386") || contains(t, m, "JEMM") || contains(t, m, "QEMM") ||
           contains(t, m, "UMBPCI") || contains(t, m, "386MAX");
}

static char drvline[3][120];

static void make_drv_lines(void)
{
    if (is_ebox) {   /* eBOX-2300SX: E000-EFFF = BIOS i kontrolery USB, wolne C800-DFFF */
        sf(drvline[0], 120, "DEVICE=%s\\JEMMEX.EXE X2MAX=8192 NOEMS X=E000-EFFF", dst);
        sf(drvline[1], 120, "DEVICE=%s\\JEMMEX.EXE X2MAX=8192 FRAME=D000 X=E000-EFFF", dst);
    } else {         /* inny komputer: JEMMEX sam szuka wolnych obszarow */
        sf(drvline[0], 120, "DEVICE=%s\\JEMMEX.EXE X2MAX=8192 NOEMS X=TEST", dst);
        sf(drvline[1], 120, "DEVICE=%s\\JEMMEX.EXE X2MAX=8192 X=TEST", dst);
    }
    sf(drvline[2], 120, "DEVICE=%s\\HIMEMX.EXE", dst);
}

/* FreeDOS FDCONFIG.SYS / CONFIG.SYS: 0 = blad, 1 = OK */
static int is_menutext(const char *t, int n) { return n >= 4 && sieq_n(t, "MENU", 4) && !(n >= 8 && (sieq_n(t, "MENUDEFA", 8) || sieq_n(t, "MENUCOLO", 8))); }
static void fd_entries(char *ml, int max, char *l, int lmax, const int *d)
{
    int k; char t[200], all[12] = "";
    ml[0] = l[0] = 0;
    for (k = 0; k < nent; k++) {
        char c[2] = { (char)('0' + d[k]), 0 };
        sf(t, sizeof t, "MENU   %c - %s\r\n", '0' + d[k], entries[k].label); if (slen(ml) + slen(t) < max) scat(ml, t);
        sf(t, sizeof t, "%c?%s\r\n", '0' + d[k], drvline[entries[k].drv]); if (slen(l) + slen(t) < lmax) scat(l, t);
        scat(all, c);
    }
    sf(t, sizeof t, "%s?DOS=HIGH,UMB\r\n", all); scat(l, t);
}
static int edit_freedos(void)
{
    int p = 0, len, used = 0, d[5], nd = 0, has_menu = 0, first_mem = -1, last_menu = -1, first_body = -1, i, ins_jem = 0;
    char allpre[12] = "", l[900], ml[600];
    while (cfg[p]) {
        int q = line_at(cfg, p, &len), w = skip_ws(cfg + p, len), pre = fd_prefix(cfg + p + w, len - w);
        const char *t = cfg + p + w;
        if (len - w >= 4 && sieq_n(t, "MENU", 4)) { has_menu = 1; if (is_menutext(t, len - w)) last_menu = p; }
        else if (first_body < 0 && len - w > 0 && t[0] != ';' && !sieq_n(t, "REM", 3)) first_body = p;
        for (i = 0; i + 1 < pre; i++) used |= 1 << (t[i] - '0');
        if (len - w >= 12 && sieq_n(t, "MENUDEFAULT=", 12) && t[12] >= '0' && t[12] <= '9') used |= 1 << (t[12] - '0');
        if (first_mem < 0 && is_memline(cfg + p, len)) first_mem = p;
        p = q;
    }
    if (!has_menu) used |= 1 << 1;               /* 1 = dotychczasowa konfiguracja */
    for (i = 1; i <= 9 && nd < nent; i++) if (!(used & (1 << i))) d[nd++] = i;
    if (nd < nent) { pr(L("W menu nie ma %u wolnych numerow (instalacja z 2 pozycjami: INSTALL /2).\n", "The menu has no %u free numbers (install with 2 entries: INSTALL /2).\n"), (u32)nent); return 0; }
    for (i = 1; i <= 9; i++) if (used & (1 << i)) { char c[2] = { (char)('0' + i), 0 }; scat(allpre, c); }
    for (i = 0; i < nent; i++) sf(ids[i], 12, "%c", '0' + d[i]);
    fd_entries(ml, sizeof ml, l, sizeof l, d);
    on = 0;
    if (!has_menu) {
        char t[120];
        sf(t, sizeof t, "MENU\r\nMENU   1 - %s\r\n", PREV_LABEL); emit(t);
        emit(ml);
        emit("MENU\r\nMENUDEFAULT=1,10\r\n");
    }
    if (first_mem < 0) first_mem = first_body;
    p = 0;
    while (cfg[p]) {
        int q = line_at(cfg, p, &len), w = skip_ws(cfg + p, len);
        const char *t = cfg + p + w;
        if (!ins_jem && p == first_mem) { emit(l); ins_jem = 1; }
        if (is_memline(cfg + p, len) && !fd_prefix(t, len - w)) { emit(allpre); emit("?"); emitn(t, len - w); emit("\r\n"); }
        else { emitn(cfg + p, len); emit("\r\n"); }
        if (has_menu && p == last_menu) emit(ml);
        p = q;
    }
    if (!ins_jem) emit(l);
    return 1;
}

/* MS-DOS CONFIG.SYS */
static void ms_items(void) { int k; char t[200]; for (k = 0; k < nent; k++) { sf(t, sizeof t, "MENUITEM=%s, %s\r\n", entries[k].block, entries[k].label); emit(t); } }
static void ms_blocks(void) { int k; char t[300]; for (k = 0; k < nent; k++) { sf(t, sizeof t, "\r\n[%s]\r\n%s\r\nDOS=HIGH,UMB\r\n", entries[k].block, drvline[entries[k].drv]); emit(t); } }
static int edit_msdos(void)
{
    int p = 0, len, has_menu = 0, mem_in_common = 0, in_common = 1, k;
    char t[200];
    while (cfg[p]) {
        int q = line_at(cfg, p, &len), w = skip_ws(cfg + p, len); const char *s = cfg + p + w;
        if (len - w >= 1 && s[0] == '[') {
            in_common = (len - w >= 8 && sieq_n(s, "[COMMON]", 8));
            if (len - w >= 6 && sieq_n(s, "[MENU]", 6)) has_menu = 1;
        } else if (in_common && is_memline(cfg + p, len) && !(len - w >= 4 && sieq_n(s, "DOS=", 4))) mem_in_common = 1;
        p = q;
    }
    for (k = 0; k < nent; k++) scpy(ids[k], entries[k].block);
    on = 0;
    if (!has_menu) {
        sf(t, sizeof t, "[MENU]\r\nMENUITEM=ORIG, %s\r\n", PREV_LABEL); emit(t);
        ms_items();
        emit("MENUDEFAULT=ORIG,10\r\n\r\n[ORIG]\r\n");
        p = 0;
        while (cfg[p]) { int q = line_at(cfg, p, &len); if (is_memline(cfg + p, len)) { emitn(cfg + p, len); emit("\r\n"); } p = q; }
        ms_blocks();
        emit("\r\n[COMMON]\r\n");
        p = 0;
        while (cfg[p]) { int q = line_at(cfg, p, &len); if (!is_memline(cfg + p, len)) { emitn(cfg + p, len); emit("\r\n"); } p = q; }
        return 1;
    }
    if (mem_in_common) {
        pr(L("CONFIG.SYS ma juz menu, a menedzer pamieci jest w [COMMON] - nie zmieniam tego automatycznie.\n",
             "CONFIG.SYS already has a menu and the memory manager is in [COMMON] - not changed automatically.\n"));
        return 0;
    }
    {   int in_menu = 0, last_item = -1;
        p = 0;
        while (cfg[p]) {
            int q = line_at(cfg, p, &len), w = skip_ws(cfg + p, len); const char *s = cfg + p + w;
            if (len - w >= 1 && s[0] == '[') in_menu = (len - w >= 6 && sieq_n(s, "[MENU]", 6));
            else if (in_menu && len - w >= 9 && sieq_n(s, "MENUITEM=", 9)) last_item = p;
            p = q;
        }
        if (last_item < 0) { pr(L("Menu bez pozycji MENUITEM.\n", "Menu without MENUITEM entries.\n")); return 0; }
        p = 0;
        while (cfg[p]) { int q = line_at(cfg, p, &len); emitn(cfg + p, len); emit("\r\n"); if (p == last_item) ms_items(); p = q; }
        ms_blocks();
    }
    return 1;
}

/* AUTOEXEC: wywolanie SBUSB.BAT po wybraniu naszych pozycji */
static int edit_autoexec(const char *name)
{
    int n = read_text(name, aut, sizeof aut), p = 0, len, k;
    char l[300];
    if (n < 0) { aut[0] = 0; n = 0; }
    if (contains(aut, n, "SBUSB.BAT")) return 1;
    on = 0;
    if (n) {
        int q = line_at(aut, 0, &len), w = skip_ws(aut, len);
        if (len - w >= 9 && sieq_n(aut + w, "@ECHO OFF", 9)) { emitn(aut, len); emit("\r\n"); p = q; }
        else emit("@ECHO OFF\r\n");
    } else emit("@ECHO OFF\r\n");
    emit("REM SBUSB-START\r\n");
    for (k = 0; k < nent; k++) { sf(l, sizeof l, "IF \"%%CONFIG%%\"==\"%s\" CALL %s\\SBUSB.BAT %s\r\n", ids[k], dst, entries[k].mode); emit(l); }
    emit("REM SBUSB-END\r\n");
    while (aut[p]) { int q = line_at(aut, p, &len); emitn(aut + p, len); emit("\r\n"); p = q; }
    emit("REM SBUSB-PATH\r\n");   /* PATH na koncu - wczesniejsza linia PATH by go nadpisala */
    for (k = 0; k < nent; k++) { sf(l, sizeof l, "IF \"%%CONFIG%%\"==\"%s\" PATH %%PATH%%;%s\r\n", ids[k], dst); emit(l); }
    return write_text(name, out);
}

static void find_autoexec(char *name, const char *cfgtext)
{   /* FreeDOS: SHELL=...COMMAND.COM ... /P=C:\FDAUTO.BAT */
    int p = 0, len;
    sf(name, 80, "%c:\\AUTOEXEC.BAT", bootdrv);
    while (cfgtext[p]) {
        int q = line_at(cfgtext, p, &len), i;
        if (contains(cfgtext + p, len, "SHELL")) {
            for (i = 0; i + 3 < len; i++) if (cfgtext[p + i] == '/' && up(cfgtext[p + i + 1]) == 'P' && cfgtext[p + i + 2] == '=') {
                int k = 0; i += 3;
                while (i < len && cfgtext[p + i] != ' ' && k < 79) name[k++] = cfgtext[p + i++];
                name[k] = 0;
                return;
            }
        }
        p = q;
    }
}

/* SBUSB.BAT [TEST|DIAG|BEZ] */
static void bl(const char *f, const char *a) { char t[300]; sf(t, sizeof t, f, a, a, a); emit(t); emit("\r\n"); }
static void write_bat(void)
{
    char n[100];
    on = 0;
    bl("@ECHO OFF", 0);
    bl(L("REM SBUSB 1.0 Lite: SBEMU (Sound Blaster 16) z karta dzwiekowa USB.  SBUSB.BAT [TEST|DIAG|BEZ]",
         "REM SBUSB 1.0 Lite: SBEMU (Sound Blaster 16) with a USB sound card.  SBUSB.BAT [TEST|DIAG|BEZ]"), 0);
    bl(L("REM Bufor karty USB = opoznienie dzwieku: L16 (najmniejsze) .. L128. Przy trzaskach zwieksz.",
         "REM USB card buffer = sound latency: L16 (lowest) .. L128. Increase it if you hear crackles."), 0);
    bl("SET USBBUF=L32", 0);
    bl(L("REM Przerwanie emulowanego SB: 5, 7 albo 9", "REM Emulated SB interrupt: 5, 7 or 9"), 0);
    bl("SET SBIRQ=5", 0);
    bl("IF \"%%1\"==\"BEZ\" GOTO BEZ", 0);
    bl("%s\\JLOAD %s\\QPIEMU.DLL", dst);
    bl("LH %s\\HDPMI32I -r -x", dst);
    bl("SET BLASTER=A220 I%%SBIRQ%% D1 H5 T6", 0);
    bl("IF \"%%1\"==\"DIAG\" GOTO DIAG", 0);
    bl("SET USBPCM=%%USBBUF%%", 0);
    bl("IF \"%%1\"==\"TEST\" SET USBPCM=R20 %%USBBUF%%", 0);
    bl("%s\\PAMIEC /HIGH", dst);
    bl("%s\\SBEMU /T6 /H5 /I%%SBIRQ%%", dst);
    bl("%s\\PAMIEC /LOW", dst);
    bl("IF EXIST \\USBMIX.CFG %s\\USBMIX /L", dst);
    bl("ECHO.", 0);
    bl("IF \"%%1\"==\"TEST\" GOTO TEST", 0);
    bl(L("ECHO  Sound Blaster 16 gotowy: A220 I%%SBIRQ%% D1 H5 T6", "ECHO  Sound Blaster 16 ready: A220 I%%SBIRQ%% D1 H5 T6"), 0);
    bl(L("ECHO  USBMIX - mikser, SBTEST - test SB, PAMIEC - pamiec", "ECHO  USBMIX - mixer, SBTEST - SB test, PAMIEC - memory"), 0);
    bl("GOTO END", 0);
    bl(":TEST", 0);
    bl(L("ECHO  Test karty (nagrywane sa ostatnie 20 s):", "ECHO  Card test (the last 20 s are recorded):"), 0);
    bl(L("ECHO  SBMIX   test miksowania - sam podaje OK albo BLAD", "ECHO  SBMIX   mixing test - prints OK or BLAD (error)"), 0);
    bl(L("ECHO  SBMIX /L  pomiar opoznienia", "ECHO  SBMIX /L  latency measurement"), 0);
    bl(L("ECHO  SBTEST  test wszystkich trybow SB", "ECHO  SBTEST  all SB modes"), 0);
    bl(L("ECHO  USBMIX  mikser, mierniki, odsluch LINE IN", "ECHO  USBMIX  mixer, meters, LINE IN monitor"), 0);
    bl(L("ECHO  USBREC  mierniki, USBREC /W zapis WAV", "ECHO  USBREC  meters, USBREC /W saves WAV files"), 0);
    bl("GOTO END", 0);
    bl(":DIAG", 0);
    bl("SET USBPCM=V R20 %%USBBUF%%", 0);
    bl(L("ECHO  Diagnostyka: sprawdz klawiature, potem wpisz:", "ECHO  Diagnostics: check the keyboard, then type:"), 0);
    bl("ECHO     %s\\SBEMU /T6 /H5 /I%%SBIRQ%%", dst);
    bl(L("ECHO  (USBPCM=V pokazuje kazdy krok sterownika USB)", "ECHO  (USBPCM=V shows every step of the USB driver)"), 0);
    bl("GOTO END", 0);
    bl(":BEZ", 0);
    bl(L("ECHO  Bez SBEMU: USBTONE - test karty tonem, USBTONE /LR - lewy/prawy,",
         "ECHO  Without SBEMU: USBTONE - tone test, USBTONE /LR - left/right,"), 0);
    bl(L("ECHO  USBTONE /LOOP - pomiar przez kabel, USBTONE /I - opis karty, CPUTEST",
         "ECHO  USBTONE /LOOP - loopback measurement, USBTONE /I - card description, CPUTEST"), 0);
    bl(":END", 0);
    sf(n, sizeof n, "%s\\SBUSB.BAT", dst);
    if (!write_text(n, out)) pr(L("BLAD: nie mozna zapisac %s\n", "ERROR: cannot write %s\n"), n);
}

static int ask(const char *q)
{
    u8 k;
    if (opt_y) return 1;
    pr("%s %s ", q, L("(T/N)", "(Y/N)"));
    k = (u8)getkey();
    pr("%c\n", (k >= 32 && k < 127) ? k : '-');
    return k == 't' || k == 'T' || k == 'y' || k == 'Y';
}
static void restore(const char *name, const char *bak)
{
    if (!dos_exists(bak)) return;
    if (read_text(bak, (char *)cbuf, sizeof cbuf) < 0) return;
    if (write_text(name, (const char *)cbuf)) pr(L("  przywrocono %s\n", "  restored %s\n"), name);
}

int main(void)
{
    char cmd[128], cfgname[80], cfgbak[80], autname[80], autbak[80];
    int n = *(volatile u8 *)0x80, i, ok = 1;
    for (i = 0; i < n && i < 126; i++) cmd[i] = up(*(volatile char *)(0x81 + i));
    cmd[i] = 0;
    {   u32 ax = 0x3305, dx = 0; asm volatile("int $0x21" : "+a"(ax), "=d"(dx) :: "cc"); if ((dx & 0xFF) >= 1 && (dx & 0xFF) <= 26) bootdrv = (char)('A' + (dx & 0xFF) - 1); }
    sf(dst, sizeof dst, "%c:\\SBUSB", bootdrv);
    for (i = 0; cmd[i]; i++) if (cmd[i] == '/') {
        char c = cmd[i + 1];
        if (c == 'Y') opt_y = 1;
        else if (c == 'N') opt_n = 1;
        else if (c == 'U') opt_u = 1;
        else if (c == 'F') full = 1;
        else if (c == '2') full = 0;
        else if (c == 'M' && cmd[i + 2] == 'S') opt_ms = 1;
        else if (c == 'D' && cmd[i + 2] == ':') { int k = 0; i += 3; while (cmd[i] && cmd[i] != ' ' && k < 70) dst[k++] = cmd[i++]; dst[k] = 0; i--; if (k > 1 && dst[k - 1] == '\\') dst[k - 1] = 0; }
        else if (c == '?' || c == 'H') {
            pr(L("INSTALL - instalator SBUSB (FreeDOS, MS-DOS 6.22/7.1)\n"
                 "  INSTALL           instalacja do <dysk startowy>:\\SBUSB\n"
                 "  INSTALL /F        pelne menu: 5 sprawdzonych konfiguracji (jak w obrazie eBOX)\n"
                 "  INSTALL /2        tylko 2 pozycje: gry XMS, gry XMS + EMS\n"
                 "  INSTALL /D:C:\\SB  inny katalog\n"
                 "  INSTALL /Y        bez pytan (z /F albo /2; domyslnie pelne menu)\n"
                 "  INSTALL /N        tylko pliki, bez zmian CONFIG/AUTOEXEC\n"
                 "  INSTALL /U        odinstalowanie: przywraca CONFIG i AUTOEXEC\n",
                 "INSTALL - SBUSB installer (FreeDOS, MS-DOS 6.22/7.1)\n"
                 "  INSTALL           install to <boot drive>:\\SBUSB\n"
                 "  INSTALL /F        full menu: 5 tested configurations (as in the eBOX image)\n"
                 "  INSTALL /2        only 2 entries: games XMS, games XMS + EMS\n"
                 "  INSTALL /D:C:\\SB  other directory\n"
                 "  INSTALL /Y        no questions (with /F or /2; full menu by default)\n"
                 "  INSTALL /N        files only, CONFIG/AUTOEXEC unchanged\n"
                 "  INSTALL /U        uninstall: restores CONFIG and AUTOEXEC\n"));
            return 0;
        }
    }
    {   u16 env = *(volatile u16 *)0x2C, o = 0; int k = 0, last = 0;
        while (peekb(env, o) || peekb(env, (u16)(o + 1))) o++;
        o += 4;
        while (peekb(env, o) && k < 78) { srcdir[k] = (char)peekb(env, o++); if (srcdir[k] == '\\' || srcdir[k] == ':') last = k + 1; k++; }
        srcdir[last] = 0; }
    {   u32 ax = 0x3000, bx = 0; asm volatile("int $0x21" : "+a"(ax), "+b"(bx) :: "cc", "ecx");
        is_fd = ((bx >> 8) & 0xFF) == 0xFD && !opt_ms;
        pr(L("SBUSB 1.0 Lite - instalator.  (c) 2026 Lukasz Bolesta, z pomoca Claude\n",
              "SBUSB 1.0 Lite - installer.  (c) 2026 Lukasz Bolesta, with the help of Claude\n"));
        pr(L("Licencje: LICENCJA.TXT (MIT; SBEMU: GNU GPL v2)\n", "Licences: LICENSE.TXT (MIT; SBEMU: GNU GPL v2)\n"));
        pr(L("System: %s %u.%u, dysk startowy %c:\n", "System: %s %u.%u, boot drive %c:\n"), is_fd ? "FreeDOS" : "MS-DOS", ax & 0xFF, (ax >> 8) & 0xFF, bootdrv); }
    {   u32 v; outl(0xCF8, 0x80000000u); v = inl(0xCFC); is_ebox = (v & 0xFFFF) == 0x17F3;
        if (is_ebox) pr(L("Komputer z procesorem DM&P Vortex86 (eBOX) - ustawienia JEMMEX dla eBOX-2300SX\n",
                          "DM&P Vortex86 computer (eBOX) - JEMMEX settings for the eBOX-2300SX\n")); }

    if (is_fd) {
        sf(cfgname, sizeof cfgname, "%c:\\FDCONFIG.SYS", bootdrv);
        if (!dos_exists(cfgname)) sf(cfgname, sizeof cfgname, "%c:\\CONFIG.SYS", bootdrv);
    } else sf(cfgname, sizeof cfgname, "%c:\\CONFIG.SYS", bootdrv);
    scpy(cfgbak, cfgname); scpy(cfgbak + slen(cfgbak) - 3, "SBU");
    if (read_text(cfgname, cfg, sizeof cfg) < 0) cfg[0] = 0;
    find_autoexec(autname, cfg);
    scpy(autbak, autname); scpy(autbak + slen(autbak) - 3, "SBU");

    if (opt_u) {
        if (!ask(L("Przywrocic konfiguracje sprzed instalacji?", "Restore the configuration from before installation?"))) return 1;
        restore(cfgname, cfgbak); restore(autname, autbak);
        pr(L("Pliki w %s mozesz usunac recznie.\n", "You may delete the files in %s.\n"), dst);
        return 0;
    }
    pr(L("Katalog: %s\nKonfiguracja: %s, %s\n", "Directory: %s\nConfiguration: %s, %s\n"), dst, cfgname, autname);
    if (full < 0) {
        if (opt_y) full = 1;
        else {
            pr(L("Menu startowe:\n  pelne - 5 sprawdzonych konfiguracji: gry XMS, gry XMS + EMS, test karty,\n          karta bez SBEMU, diagnostyka (jak w obrazie dla eBOX-a)\n  krotkie - 2 pozycje: gry XMS, gry XMS + EMS\n",
                 "Boot menu:\n  full  - 5 tested configurations: games XMS, games XMS + EMS, card test,\n          card without SBEMU, diagnostics (as in the eBOX image)\n  short - 2 entries: games XMS, games XMS + EMS\n"));
            full = ask(L("Pelne menu?", "Full menu?"));
        }
    }
    nent = full ? 5 : 2;
    if (!ask(L("Zainstalowac?", "Install?"))) return 1;

    dos_mkdir(dst);
    pr(L("Kopiowanie plikow...\n", "Copying files...\n"));
    for (i = 0; files[i]; i++) if (!copy_file(files[i])) ok = 0;
    if (!ok) { pr(L("Instalacja niepelna.\n", "Installation incomplete.\n")); return 2; }
    write_bat();
    pr(L("Pliki skopiowane, utworzono %s\\SBUSB.BAT\n", "Files copied, created %s\\SBUSB.BAT\n"), dst);

    make_drv_lines();
    if (opt_n) { pr(L("/N: CONFIG i AUTOEXEC bez zmian. Recznie: zaladuj JEMMEX zamiast HIMEM/EMM386 i wywolaj SBUSB.BAT.\n",
                      "/N: CONFIG and AUTOEXEC unchanged. Manually: load JEMMEX instead of HIMEM/EMM386 and CALL SBUSB.BAT.\n")); return 0; }
    if (contains(cfg, slen(cfg), "SBUSB")) {
        pr(L("Konfiguracja juz zawiera SBUSB - zaktualizowano tylko pliki.\n", "The configuration already contains SBUSB - only the files were updated.\n"));
        return 0;
    }
    if (!(is_fd ? edit_freedos() : edit_msdos())) {
        char t[700], nm[100];
        sf(t, sizeof t, L("Dodaj recznie do %s nowa pozycje menu z liniami:\r\n  %s\r\n  DOS=HIGH,UMB\r\n(bez HIMEM/EMM386 w tej pozycji), a w AUTOEXEC.BAT dla niej:\r\n  CALL %s\\SBUSB.BAT\r\n",
                          "Add manually to %s a new menu entry with the lines:\r\n  %s\r\n  DOS=HIGH,UMB\r\n(without HIMEM/EMM386 in that entry), and in AUTOEXEC.BAT for it:\r\n  CALL %s\\SBUSB.BAT\r\n"),
           cfgname, drvline[0], dst);
        sf(nm, sizeof nm, "%s\\%s", dst, L("RECZNIE.TXT", "MANUAL.TXT")); write_text(nm, t);
        pr(L("Instrukcja: %s\n", "Instructions: %s\n"), nm);
        return 3;
    }
    if (!backup(cfgname, cfgbak) || !backup(autname, autbak)) { pr(L("BLAD: nie mozna zrobic kopii - nic nie zmieniono\n", "ERROR: cannot create backups - nothing changed\n")); return 2; }
    dos_attr0(cfgname);
    if (!write_text(cfgname, out)) { pr(L("BLAD zapisu %s\n", "write error %s\n"), cfgname); return 2; }
    if (!edit_autoexec(autname)) { pr(L("BLAD zapisu %s\n", "write error %s\n"), autname); return 2; }
    pr(L("\nGotowe. Kopie: %s, %s\nNowe pozycje menu startowego:\n", "\nDone. Backups: %s, %s\nNew boot menu entries:\n"), cfgbak, autbak);
    for (i = 0; i < nent; i++) pr("  %s - %s\n", ids[i], entries[i].label);
    pr(L("Odinstalowanie: %sINSTALL /U\nGdyby system nie wystartowal: F5 przy starcie pomija CONFIG/AUTOEXEC.\n",
         "Uninstall: %sINSTALL /U\nIf the system does not start: press F5 at boot to skip CONFIG/AUTOEXEC.\n"), srcdir);
    return 0;
}
