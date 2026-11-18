/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * USBMIX.COM - mikser z suwakami dla SBEMU z karta USB (tryb tekstowy). Licencja: MIT.
 * Dziala w trybie rzeczywistym i V86 (pod JEMMEX).
 *
 *  - glosnosc glowna i dzwieku cyfrowego: rejestry miksera SB16 (30h-33h) przez porty
 *    BLASTER+4/+5 - tak jak programy miksera do prawdziwego SB16; 2 dB na krok,
 *  - odsluch LINE IN: pole mon_gain w bloku sterujacym sterownika USB (usbpcmct.h),
 *  - mierniki szczytowe wyjscia i LINE IN z tego samego bloku.
 *
 *   USBMIX       mikser (Esc - wyjscie)
 *   USBMIX /L    zaladuj zapisane ustawienia (USBMIX.CFG w katalogu glownym) bez ekranu
 *   USBMIX /?    pomoc
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed short s16; typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end
#include "dbtab.h"
#include "usbpcmct.h"

void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static u8 peekb(u16 seg, u16 off) { u8 v; asm volatile("movw %1,%%fs\n\tmovb %%fs:(%2),%0" : "=q"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u16 peekw(u16 seg, u16 off) { u16 v; asm volatile("movw %1,%%fs\n\tmovw %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u32 peekl(u16 seg, u16 off) { u32 v; asm volatile("movw %1,%%fs\n\tmovl %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static void pokew(u16 seg, u16 off, u16 v) { asm volatile("movw %0,%%fs\n\tmovw %1,%%fs:(%2)" :: "r"(seg), "r"(v), "r"((u32)off) : "memory"); }
static void pokel(u16 seg, u16 off, u32 v) { asm volatile("movw %0,%%fs\n\tmovl %1,%%fs:(%2)" :: "r"(seg), "r"(v), "r"((u32)off) : "memory"); }
static u16 ticks(void) { return peekw(0x40, 0x6C); }
static u16 my_seg(void) { u16 s; asm volatile("movw %%cs,%0" : "=r"(s)); return s; }

static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_read(u16 h, void *buf, u16 len)
{ u32 ax = 0x3F00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_open(const char *n, int create)
{ u32 ax = create ? 0x3C00 : 0x3D00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static int kbhit(void) { u32 ax = 0x0100; u8 nz; asm volatile("int $0x16; setnz %1" : "+a"(ax), "=qm"(nz) :: "memory", "cc"); return nz; }
static u16 getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }

static int fmt(char *o, int max, const char *f, va_list ap)
{
    int n = 0; static const char hx[] = "0123456789ABCDEF";
    while (*f) {
        char c = *f++;
        if (c != '%') { if (n < max - 1) o[n++] = c; continue; }
        { int zero = 0, width = 0; char tmp[16]; int tl = 0; const char *s = tmp; int sl, i;
          if (*f == '0') { zero = 1; f++; }
          while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
          c = *f++;
          if (c == 'u' || c == 'd') { u32 v = va_arg(ap, u32); int neg = 0; if (c == 'd' && (s32)v < 0) { neg = 1; v = -v; }
              do { tmp[tl++] = (char)('0' + v % 10); v /= 10; } while (v); if (neg) tmp[tl++] = '-';
              for (i = 0; i < tl / 2; i++) { char t = tmp[i]; tmp[i] = tmp[tl - 1 - i]; tmp[tl - 1 - i] = t; } sl = tl; }
          else if (c == 'x') { u32 v = va_arg(ap, u32); do { tmp[tl++] = hx[v & 15]; v >>= 4; } while (v);
              for (i = 0; i < tl / 2; i++) { char t = tmp[i]; tmp[i] = tmp[tl - 1 - i]; tmp[tl - 1 - i] = t; } sl = tl; }
          else if (c == 's') { s = va_arg(ap, const char *); sl = str_len(s); }
          else if (c == 'c') { tmp[0] = (char)va_arg(ap, int); sl = 1; }
          else { tmp[0] = c; sl = 1; }
          while (sl < width && n < max - 1) { o[n++] = zero ? '0' : ' '; width--; }
          for (i = 0; i < sl && n < max - 1; i++) o[n++] = s[i]; }
    }
    o[n] = 0; return n;
}
static void pr(const char *f, ...)
{
    char b[300], o[600]; va_list ap; int n, i, m = 0;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    for (i = 0; i < n; i++) { if (b[i] == '\n') o[m++] = '\r'; o[m++] = b[i]; }
    dos_write(1, o, (u16)m);
}
static void sfmt(char *o, int max, const char *f, ...) { va_list ap; va_start(ap, f); fmt(o, max, f, ap); va_end(ap); }

/* ------------------------------------------------------------------ */
/* Sound Blaster: BLASTER i mikser                                     */
/* ------------------------------------------------------------------ */
static u16 sbbase = 0x220, sbtype = 6;
static int parse_blaster(void)
{
    u16 envseg = *(volatile u16 *)0x2C, o = 0;
    while (o < 4096 && peekb(envseg, o)) {
        static const char nm[] = "BLASTER=";
        int i;
        for (i = 0; i < 8; i++) if ((char)peekb(envseg, o + i) != nm[i]) break;
        if (i == 8) {
            u16 p = o + 8;
            while (peekb(envseg, p)) {
                char c = (char)(peekb(envseg, p++) | 32), d; u16 v = 0, hex = (c == 'a');
                while ((d = (char)peekb(envseg, p)) && d != ' ') {
                    if (d >= '0' && d <= '9') v = v * (hex ? 16 : 10) + (d - '0');
                    else if (hex && ((d | 32) >= 'a' && (d | 32) <= 'f')) v = v * 16 + ((d | 32) - 'a' + 10);
                    p++;
                }
                if (d == ' ') p++;
                if (c == 'a') sbbase = v; else if (c == 't') sbtype = v;
            }
            return 1;
        }
        while (peekb(envseg, o)) o++;
        o++;
    }
    return 0;
}
static void mix_wr(u8 r, u8 v) { outb(sbbase + 4, r); outb(sbbase + 5, v); }
static u8 mix_rd(u8 r) { outb(sbbase + 4, r); return inb(sbbase + 5); }
static int sb_present(void)
{
    u8 o = mix_rd(0x32), v;
    mix_wr(0x32, 0x58); v = mix_rd(0x32); mix_wr(0x32, o);
    return v == 0x58;
}

/* ------------------------------------------------------------------ */
/* blok sterujacy sterownika USB                                       */
/* ------------------------------------------------------------------ */
#define OFS(f) ((u16)__builtin_offsetof(usbpcm_ctl_t, f))
#define STO(i, f) ((u16)(__builtin_offsetof(usbpcm_ctl_t, st) + (i) * sizeof(usbpcm_stream_t) + __builtin_offsetof(usbpcm_stream_t, f)))
static u16 cseg;
static int find_block(void)
{
    static const char sig[] = USBPCM_CTL_SIG;
    u16 s, me = my_seg(); int k;
    for (s = 0x0050; s < 0xA000; s++) {
        if (s >= me && s < me + 0x1000) continue;
        for (k = 0; k < 8; k++) if (peekb(s, (u16)k) != (u8)sig[k]) break;
        if (k < 8) continue;
        if (peekl(s, OFS(self)) != (u32)s * 16) continue;
        if (peekw(s, OFS(version)) != USBPCM_CTL_VERSION) return 0;
        cseg = s; return 1;
    }
    return 0;
}

/* odsluch LINE IN: krok 0 = wylaczony, 1..24 = -46..0 dB co 2 dB */
static const u16 montab[25] = {0,1,2,2,3,3,4,5,6,8,10,13,16,20,26,32,41,51,64,81,102,128,162,203,256};
static int mon_step(void) { u16 g = peekw(cseg, OFS(mon_gain)); int i, b = 0; for (i = 0; i < 25; i++) if (montab[i] <= g) b = i; return g ? (b ? b : 1) : 0; }
static void mon_set(int i) { if (i < 0) i = 0; if (i > 24) i = 24; pokew(cseg, OFS(mon_gain), montab[i]); }

/* ------------------------------------------------------------------ */
/* ustawienia: \USBMIX.CFG  "M=31,31 V=31,31 I=0"                      */
/* ------------------------------------------------------------------ */
static int vol[2][2];   /* [0]=glowna [1]=cyfrowy; [L/P] 0..31 */
static void read_hw(void)
{
    vol[0][0] = mix_rd(0x30) >> 3; vol[0][1] = mix_rd(0x31) >> 3;
    vol[1][0] = mix_rd(0x32) >> 3; vol[1][1] = mix_rd(0x33) >> 3;
}
static void write_hw(void)
{
    mix_wr(0x30, (u8)(vol[0][0] << 3)); mix_wr(0x31, (u8)(vol[0][1] << 3));
    mix_wr(0x32, (u8)(vol[1][0] << 3)); mix_wr(0x33, (u8)(vol[1][1] << 3));
}
static int save_cfg(int have_usb)
{
    char b[64]; int h = dos_open("\\USBMIX.CFG", 1), n;
    if (h < 0) return 0;
    sfmt(b, sizeof b, "M=%u,%u V=%u,%u I=%u\r\n", (u32)vol[0][0], (u32)vol[0][1], (u32)vol[1][0], (u32)vol[1][1],
         (u32)(have_usb ? mon_step() : 0));
    n = str_len(b);
    dos_write((u16)h, b, (u16)n); dos_close(h);
    return 1;
}
static int load_cfg(int have_usb)
{
    char b[80]; int h = dos_open("\\USBMIX.CFG", 0), n, i, *p = 0, cnt = 0, mon = -1;
    if (h < 0) return 0;
    n = dos_read((u16)h, b, sizeof b - 1); dos_close(h);
    if (n <= 0) return 0;
    b[n] = 0;
    for (i = 0; i < n; i++) {
        char c = b[i] | 32;
        if ((c == 'm' || c == 'v' || c == 'i') && b[i + 1] == '=') {
            int v1 = 0, v2 = 0, two = 0; i += 2;
            while (b[i] >= '0' && b[i] <= '9') v1 = v1 * 10 + (b[i++] - '0');
            if (b[i] == ',') { two = 1; i++; while (b[i] >= '0' && b[i] <= '9') v2 = v2 * 10 + (b[i++] - '0'); }
            if (!two) v2 = v1;
            if (v1 > 31) { v1 = 31; } if (v2 > 31) { v2 = 31; }
            if (c == 'i') mon = v1 > 24 ? 24 : v1;
            else { p = vol[c == 'v']; p[0] = v1; p[1] = v2; cnt++; }
        }
    }
    if (cnt) write_hw();
    if (have_usb && mon >= 0) mon_set(mon);
    return 1;
}

/* ------------------------------------------------------------------ */
/* ekran (bezposrednio do pamieci obrazu)                              */
/* ------------------------------------------------------------------ */
static u16 vseg = 0xB800;
#define A_NORM 0x07
#define A_HI   0x0F
#define A_SEL  0x1F
#define A_TITLE 0x1E
#define A_BAR  0x0A
#define A_BARHI 0x0E
#define A_RED  0x0C
#define A_DIM  0x08
static void putc_at(int x, int y, char c, u8 a) { pokew(vseg, (u16)((y * 80 + x) * 2), (u16)(((u16)a << 8) | (u8)c)); }
static void puts_at(int x, int y, const char *s, u8 a) { while (*s && x < 80) putc_at(x++, y, *s++, a); }
static void fill_at(int x, int y, int n, char c, u8 a) { while (n-- > 0 && x < 80) putc_at(x++, y, c, a); }
static void printf_at(int x, int y, u8 a, const char *f, ...) { char b[100]; va_list ap; va_start(ap, f); fmt(b, sizeof b, f, ap); va_end(ap); puts_at(x, y, b, a); }
static void cls(void) { int i; for (i = 0; i < 80 * 25; i++) pokew(vseg, (u16)(i * 2), 0x0720); }
static void cursor(int on) { u32 ax = 0x0100, cx = on ? 0x0607 : 0x2000; asm volatile("int $0x10" : "+a"(ax), "+c"(cx) :: "cc"); }
static void gotoxy(int x, int y) { u32 ax = 0x0200, bx = 0, dx = ((u32)y << 8) | (u32)x; asm volatile("int $0x10" : "+a"(ax), "+b"(bx), "+d"(dx) :: "cc"); }

static int db_of(u16 peak) { int i; if (!peak) return 100; for (i = 0; i < 100; i++) if (peak >= dbtab[i]) return i; return 100; }

#define BARX 26
#define BARW 31
static void slider(int y, int v, int max, int sel, int mark)
{
    int i;
    putc_at(BARX - 1, y, '[', sel ? A_HI : A_NORM);
    for (i = 0; i < BARW; i++) {
        int on = i < v * BARW / max + (v == max ? 0 : 0);
        putc_at(BARX + i, y, on ? (char)0xDB : (char)0xFA, on ? (sel ? A_BARHI : A_BAR) : A_DIM);
    }
    putc_at(BARX + BARW, y, ']', sel ? A_HI : A_NORM);
    putc_at(BARX - 3, y, mark ? (char)0x10 : ' ', A_HI);
}
static void meter(int y, const char *name, u16 peak, u16 *hold)
{
    int db = db_of(peak), n = db >= 60 ? 0 : (60 - db) * BARW / 60, i;
    if (n > *hold) *hold = (u16)n;
    puts_at(4, y, name, A_NORM);
    putc_at(BARX - 1, y, '[', A_NORM);
    for (i = 0; i < BARW; i++) {
        u8 a = i >= BARW - 3 ? A_RED : (i >= BARW - 8 ? A_BARHI : A_BAR);
        char c = i < n ? (char)0xFE : (i == *hold - 1 ? (char)0xB3 : ' ');
        putc_at(BARX + i, y, c, i < n || i == *hold - 1 ? a : A_DIM);
    }
    putc_at(BARX + BARW, y, ']', A_NORM);
    if (db >= 100) printf_at(BARX + BARW + 2, y, A_DIM, "cisza       ");
    else printf_at(BARX + BARW + 2, y, db == 0 ? A_RED : A_NORM, "-%2u dBFS%s", (u32)db, db == 0 ? " MAX" : "    ");
}
static void dbtext(char *b, int v) { if (!v) sfmt(b, 16, "wycisz."); else sfmt(b, 16, "%d dB", 2 * v - 62); }

static const char *const help[] = {
    "Strzalki gora/dol - wybor     lewo/prawo - zmiana o 2 dB     PgUp/PgDn - o 10 dB",
    "Tab - oba kanaly/lewy/prawy   M - wycisz/przywroc   S - zapisz   Esc - wyjscie",
};

static int run_ui(int have_usb)
{
    int sel = 0, ch = 0, nsel = have_usb && (peekw(cseg, OFS(flags)) & UPF_HAS_IN) ? 3 : 2, quit = 0, i;
    int muted[2] = {0, 0}, saved[2][2], fb = 0, fbmsg = 0;
    u16 hold[4] = {0, 0, 0, 0}, t0 = ticks(), th = ticks();
    char msg[80] = "";
    read_hw();
    cls(); cursor(0);
    fill_at(0, 0, 80, ' ', A_TITLE);
    puts_at(1, 0, "USBMIX - mikser Sound Blaster / karta USB (SBEMU)", A_TITLE);
    puts_at(66, 0, "Esc - wyjscie", A_TITLE);
    puts_at(2, 2, "GLOSNOSC", A_HI);
    puts_at(2, 11, "MIERNIKI (szczyt)", A_HI);
    for (i = 0; i < 2; i++) puts_at(0, 22 + i, help[i], A_DIM);
    if (!have_usb) puts_at(4, 12, "Brak sterownika USB (SBEMU z karta USB) - mierniki niedostepne.", A_DIM);
    for (;;) {
        char b[24];
        /* suwaki */
        for (i = 0; i < 2; i++) {
            int y = 3 + i * 3;
            static const char *const nm[2] = { "Glowna", "Dzwiek cyfrowy" };
            puts_at(4, y, nm[i], sel == i ? A_HI : A_NORM);
            puts_at(4, y + 1, i ? "(SB16 voice)" : "(SB16 master)", A_DIM);
            slider(y, vol[i][0], 31, sel == i, sel == i && ch != 2);
            slider(y + 1, vol[i][1], 31, sel == i, sel == i && ch != 1);
            puts_at(BARX - 4, y, "L", A_DIM); puts_at(BARX - 4, y + 1, "P", A_DIM);
            dbtext(b, vol[i][0]); printf_at(BARX + BARW + 2, y, A_NORM, "%s        ", b);
            dbtext(b, vol[i][1]); printf_at(BARX + BARW + 2, y + 1, A_NORM, "%s        ", b);
            if (muted[i]) puts_at(BARX + BARW + 12, y, "[wyciszone]", A_RED); else fill_at(BARX + BARW + 12, y, 11, ' ', A_NORM);
        }
        if (nsel == 3) {
            int m = mon_step();
            puts_at(4, 9, "Odsluch LINE IN", sel == 2 ? A_HI : A_NORM);
            slider(9, m, 24, sel == 2, sel == 2);
            if (m) printf_at(BARX + BARW + 2, 9, A_NORM, "%d dB     ", 2 * m - 48); else puts_at(BARX + BARW + 2, 9, "wylaczony ", A_NORM);
        }
        puts_at(40, 2, ch == 0 ? "kanaly: oba  " : ch == 1 ? "kanal: lewy  " : "kanal: prawy ", A_DIM);
        /* mierniki */
        if (have_usb && (u16)(ticks() - t0) >= 1) {
            u16 p[4];
            t0 = ticks();
            for (i = 0; i < 2; i++) { p[2 * i] = peekw(cseg, STO(i, peak)); p[2 * i + 1] = peekw(cseg, STO(i, peak) + 2); pokel(cseg, STO(i, peak), 0); }
            if ((u16)(ticks() - th) > 9) { th = ticks(); for (i = 0; i < 4; i++) if (hold[i]) hold[i]--; }
            meter(12, "Wyjscie L", p[0], &hold[0]); meter(13, "        P", p[1], &hold[1]);
            if (peekw(cseg, OFS(flags)) & UPF_HAS_IN) { meter(15, "LINE IN L", p[2], &hold[2]); meter(16, "        P", p[3], &hold[3]); }
            else puts_at(4, 15, "LINE IN: niedostepne (karta bez wejscia 48 kHz stereo albo SET USBPCM=X)", A_DIM);
            /* ochrona przed sprzezeniem: odsluch + kabel LINE OUT -> LINE IN */
            if (peekw(cseg, OFS(mon_gain)) && (p[2] > 32000 || p[3] > 32000)) {
                if (++fb >= 15) { mon_set(0); fb = 0; fbmsg = 1;
                    sfmt(msg, sizeof msg, "Odsluch WYLACZONY: przester na LINE IN (sprzezenie przez kabel?)"); }
            } else fb = 0;
            {   u32 st = peekl(cseg, OFS(errors)), lt = peekl(cseg, OFS(late));
                printf_at(4, 18, A_DIM, "USB: karta %04x:%04x, OHCI #%u, bledy %u, spoznione ramki %u        ",
                          (u32)peekw(cseg, OFS(vid)), (u32)peekw(cseg, OFS(pid)), (u32)peekw(cseg, OFS(hcnum)), st, lt); }
        }
        fill_at(0, 20, 80, ' ', A_NORM);
        if (msg[0]) puts_at(4, 20, msg, fbmsg ? A_RED : A_HI);
        if (quit) break;
        if (!kbhit()) { u16 t = ticks(); while (t == ticks() && !kbhit()) ; continue; }
        {
            u16 k = getkey(); u8 a = (u8)k, sc = (u8)(k >> 8); int d = 0;
            msg[0] = 0; fbmsg = 0;
            if (a == 27) quit = 1;
            else if (sc == 0x48) sel = (sel + nsel - 1) % nsel;
            else if (sc == 0x50) sel = (sel + 1) % nsel;
            else if (sc == 0x4B) d = -1;
            else if (sc == 0x4D) d = 1;
            else if (sc == 0x49) d = 5;
            else if (sc == 0x51) d = -5;
            else if (a == 9) ch = (ch + 1) % 3;
            else if ((a | 32) == 'm' && sel < 2) {
                if (!muted[sel]) { saved[sel][0] = vol[sel][0]; saved[sel][1] = vol[sel][1]; vol[sel][0] = vol[sel][1] = 0; muted[sel] = 1; }
                else { vol[sel][0] = saved[sel][0]; vol[sel][1] = saved[sel][1]; muted[sel] = 0; }
                write_hw();
            } else if ((a | 32) == 'm' && sel == 2) mon_set(0);
            else if ((a | 32) == 's') sfmt(msg, sizeof msg, save_cfg(have_usb) ? "Zapisano \\USBMIX.CFG (USBMIX /L wczytuje)" : "BLAD zapisu \\USBMIX.CFG");
            if (d) {
                if (sel < 2) {
                    read_hw();      /* gra mogla zmienic mikser */
                    for (i = 0; i < 2; i++) if (ch == 0 || ch == i + 1) {
                        int v = vol[sel][i] + d;
                        if (v < 0) { v = 0; } if (v > 31) { v = 31; } vol[sel][i] = v;
                    }
                    muted[sel] = 0;
                    write_hw();
                } else {
                    int m = mon_step() + d;
                    mon_set(m);
                    if (m > 0) sfmt(msg, sizeof msg, "Odsluch LINE IN wlaczony. Z kablem LINE OUT -> LINE IN grozi sprzezenie!");
                    fbmsg = m > 0;
                }
            }
        }
        read_hw();          /* pokazuj tez zmiany zrobione przez gre */
    }
    cls(); cursor(1); gotoxy(0, 0);
    return 0;
}

int main(void)
{
    char cmd[128]; int n = *(volatile u8 *)0x80, i, o_l = 0, have_usb;
    for (i = 0; i < n && i < 126; i++) { char c = *(volatile char *)(0x81 + i); if (c >= 'a' && c <= 'z') c -= 32; cmd[i] = c; }
    cmd[i] = 0;
    for (i = 0; cmd[i]; i++) if (cmd[i] == '/' || cmd[i] == '-') {
        if (cmd[i + 1] == 'L') o_l = 1;
        else if (cmd[i + 1] == '?' || cmd[i + 1] == 'H') {
            pr("USBMIX - mikser Sound Blaster / karta USB (SBEMU)\n"
               "  USBMIX      mikser z suwakami: glosnosc glowna i dzwieku cyfrowego (2 dB na krok),\n"
               "              odsluch LINE IN, mierniki wyjscia i LINE IN\n"
               "  USBMIX /L   wczytaj zapisane ustawienia z \\USBMIX.CFG (bez ekranu)\n"
               "Gry czesto ustawiaja mikser same przy starcie - wtedy ich ustawienia wygrywaja.\n");
            return 0;
        }
    }
    if (!parse_blaster()) { pr("Brak zmiennej BLASTER - czy SBEMU jest uruchomione?\n"); return 1; }
    if (!sb_present()) { pr("Mikser Sound Blastera nie odpowiada na porcie %x (czy SBEMU jest uruchomione?)\n", (u32)sbbase); return 1; }
    have_usb = find_block();
    if (o_l) {
        if (!load_cfg(have_usb)) { pr("Brak \\USBMIX.CFG - ustawienia bez zmian\n"); return 1; }
        read_hw();
        pr("USBMIX: glowna %d/%d dB, cyfrowy %d/%d dB", 2 * vol[0][0] - 62, 2 * vol[0][1] - 62, 2 * vol[1][0] - 62, 2 * vol[1][1] - 62);
        if (have_usb && mon_step()) pr(", odsluch LINE IN %d dB", 2 * mon_step() - 48);
        pr("\n");
        return 0;
    }
    {   u32 ax = 0x0F00, bx = 0; asm volatile("int $0x10" : "+a"(ax), "+b"(bx) :: "cc");
        if ((ax & 0xFF) == 7) vseg = 0xB000;
        else if ((ax & 0xFF) != 3 && (ax & 0xFF) != 2) { u32 m = 0x0003; asm volatile("int $0x10" : "+a"(m) :: "cc", "ebx", "ecx", "edx"); } }
    return run_ui(have_usb);
}
