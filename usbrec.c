/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * USBREC.COM - nagrania i mierniki sterownika USB Audio w SBEMU (sc_usbpcm).
 * Dziala w trybie rzeczywistym i V86 (pod JEMMEX). Licencja: MIT.
 *
 * Sterownik (SET USBPCM=R20 przed SBEMU) zapisuje w XMS ostatnie N sekund:
 *   - tego, co SBEMU wysyla do karty (strumien "wyjscie", RECOUT.WAV),
 *   - wejscia LINE IN karty (strumien "wejscie", RECIN.WAV).
 * Blok sterujacy "USBPCM01" lezy w pamieci konwencjonalnej (usbpcmct.h).
 *
 *   USBREC          stan + mierniki na zywo (Esc konczy)
 *   USBREC /I       tylko stan (mozna przekierowac: USBREC /I > STAN.TXT)
 *   USBREC /W       zapisz RECOUT.WAV, RECIN.WAV i STAN.TXT (cale nagranie, do 20 s);
 *                   poprzednie pliki dostaja numer: RECOUT01.WAV, RECIN01.WAV, STAN01.TXT
 *   USBREC /W /S:10 zapisz tylko ostatnie 10 sekund
 *   USBREC /C       wyczysc nagranie (zacznij od zera)
 *   USBREC /P       wstrzymaj nagrywanie     USBREC /G   wznow
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed short s16; typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end
#include "dbtab.h"
#include "usbpcmct.h"
#include "rotate.h"

void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static u8 peekb(u16 seg, u16 off) { u8 v; asm volatile("movw %1,%%fs\n\tmovb %%fs:(%2),%0" : "=q"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u16 peekw(u16 seg, u16 off) { u16 v; asm volatile("movw %1,%%fs\n\tmovw %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u32 peekl(u16 seg, u16 off) { u32 v; asm volatile("movw %1,%%fs\n\tmovl %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static void pokew(u16 seg, u16 off, u16 v) { asm volatile("movw %0,%%fs\n\tmovw %1,%%fs:(%2)" :: "r"(seg), "r"(v), "r"((u32)off) : "memory"); }
static void pokel(u16 seg, u16 off, u32 v) { asm volatile("movw %0,%%fs\n\tmovl %1,%%fs:(%2)" :: "r"(seg), "r"(v), "r"((u32)off) : "memory"); }
static u16 ticks(void) { return peekw(0x40, 0x6C); }

static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_create(const char *n)
{ u32 ax = 0x3C00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static int kbhit(void) { u32 ax = 0x0100; u8 nz; asm volatile("int $0x16; setnz %1" : "+a"(ax), "=qm"(nz) :: "memory", "cc"); return nz; }
static u16 getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }
static u16 my_seg(void) { u16 s; asm volatile("movw %%cs,%0" : "=r"(s)); return s; }

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
static u16 outh = 1;     /* dokad pisze pr(): 1 = ekran, albo plik STAN.TXT */
static void pr(const char *f, ...)
{
    char b[256], o[520]; va_list ap; int n, i, m = 0;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    for (i = 0; i < n; i++) { if (b[i] == '\n') o[m++] = '\r'; o[m++] = b[i]; }
    dos_write(outh, o, (u16)m);
}

/* ------------------------------------------------------------------ */
/* XMS                                                                 */
/* ------------------------------------------------------------------ */
static struct { u16 off, seg; } __attribute__((packed)) xms_entry;
static int xms_init(void)
{
    u32 ax = 0x4300, b = 0, es_ = 0;
    asm volatile("int $0x2F" : "+a"(ax) :: "memory", "cc");
    if ((ax & 0xFF) != 0x80) return 0;
    ax = 0x4310;
    asm volatile("pushw %%es\n\tint $0x2F\n\txorl %%ecx,%%ecx\n\tmovw %%es,%%cx\n\tpopw %%es"
                 : "+a"(ax), "=b"(b), "=c"(es_) :: "memory", "cc");
    xms_entry.off = (u16)b; xms_entry.seg = (u16)es_;
    return 1;
}
static u32 xms_call(u32 ax, u32 *dx, u32 *bx)
{
    u32 a = ax, d = dx ? *dx : 0, b = bx ? *bx : 0;
    asm volatile("lcallw *%3" : "+a"(a), "+d"(d), "+b"(b) : "m"(xms_entry) : "memory", "cc", "ecx", "esi", "edi");
    if (dx) *dx = d & 0xFFFF;
    if (bx) *bx = b & 0xFFFF;
    return a & 0xFFFF;
}
static struct { u32 len; u16 sh; u32 soff; u16 dh; u32 doff; } __attribute__((packed)) xmove;
static int xms_to_low(u16 handle, u32 off, void *dst, u32 len)   /* XMS 0Bh */
{
    u32 a = 0x0B00, b = 0;
    xmove.len = len; xmove.sh = handle; xmove.soff = off;
    xmove.dh = 0; xmove.doff = ((u32)my_seg() << 16) | (u16)(u32)dst;
    asm volatile("lcallw *%2" : "+a"(a), "+b"(b) : "m"(xms_entry), "S"(&xmove) : "memory", "cc", "ecx", "edx", "edi");
    return (a & 0xFFFF) == 1;
}

/* ------------------------------------------------------------------ */
/* blok sterujacy                                                      */
/* ------------------------------------------------------------------ */
#define OFS(f) ((u16)__builtin_offsetof(usbpcm_ctl_t, f))
#define STO(i, f) ((u16)(__builtin_offsetof(usbpcm_ctl_t, st) + (i) * sizeof(usbpcm_stream_t) + __builtin_offsetof(usbpcm_stream_t, f)))
static u16 cseg;   /* segment bloku (offset 0) */

static int find_block(void)
{
    static const char sig[] = USBPCM_CTL_SIG;
    u16 s, me = my_seg();
    int k;
    for (s = 0x0050; s < 0xA000; s++) {
        if (s >= me && s < me + 0x1000) continue;          /* nasza wlasna kopia napisu */
        for (k = 0; k < 8; k++) if (peekb(s, (u16)k) != (u8)sig[k]) break;
        if (k < 8) continue;
        if (peekl(s, OFS(self)) != (u32)s * 16) continue;
        if (peekw(s, OFS(version)) != USBPCM_CTL_VERSION) { pr("Znaleziono blok USBPCM w innej wersji (%u) - uzyj USBREC z tego samego obrazu\n", peekw(s, OFS(version))); return 0; }
        cseg = s;
        return 1;
    }
    return 0;
}
static u32 rd(u16 o) { return peekl(cseg, o); }
static u16 rdw(u16 o) { return peekw(cseg, o); }

static int db_of(u16 peak)  /* -dBFS, 0..100 */
{
    int i;
    if (!peak) return 100;
    for (i = 0; i < 100; i++) if (peak >= dbtab[i]) return i;
    return 100;
}

static const char *sname[2] = { "wyjscie (to, co gra SBEMU)", "wejscie LINE IN" };

static void info(void)
{
    u16 fl = rdw(OFS(flags));
    int i;
    pr("Sterownik USB Audio: OHCI #%u, karta %04x:%04x, %s\n", rdw(OFS(hcnum)), rdw(OFS(vid)), rdw(OFS(pid)),
       (fl & UPF_STREAMING) ? "strumien USB dziala" : "strumien USB zatrzymany");
    pr("Przerwania %u, bledy %u, spoznione ramki %u, resynchronizacje %u\n", rd(OFS(irqs)), rd(OFS(errors)), rd(OFS(late)), rd(OFS(resyncs)));
    for (i = 0; i < 2; i++) {
        u32 sz = rd(STO(i, size)), wr = rd(STO(i, wr)), have = wr < sz ? wr : sz;
        int on = (fl & (i ? UPF_REC_IN : UPF_REC_OUT)) != 0;
        pr("%s: ", sname[i]);
        if (!on) { pr("%s\n", i && !(fl & UPF_HAS_IN) ? "brak (karta bez wejscia 48 kHz albo nie wlaczone)" : "nie nagrywane"); continue; }
        pr("ramek %u, utraconych %u, nagrano %u.%u s z %u s", rd(STO(i, frames)), rd(STO(i, lost)),
           have / 192000, (have % 192000) / 19200, sz / 192000);
        if (i) pr(", pakiety %u..%u B", rdw(OFS(in_minpkt)), rdw(OFS(in_maxpkt)));
        pr("\n");
    }
    if (rdw(OFS(ctl)) & UPC_PAUSE) pr("Nagrywanie WSTRZYMANE (USBREC /G wznawia)\n");
    else if ((fl & UPF_GATED) && (fl & (UPF_REC_OUT | UPF_REC_IN)))
        pr("Cisza na wyjsciu ponad 1 s - nagranie czeka na dzwiek (cisza nie nadpisuje nagrania)\n");
}

/* ------------------------------------------------------------------ */
/* mierniki                                                            */
/* ------------------------------------------------------------------ */
static void cursor(u16 *row) { u32 ax = 0x0300, bx = 0, dx = 0, cx = 0; asm volatile("int $0x10" : "+a"(ax), "+b"(bx), "+c"(cx), "+d"(dx) :: "cc"); *row = (u16)(dx >> 8); }
static void gotoxy(u16 row) { u32 ax = 0x0200, bx = 0, dx = (u32)row << 8; asm volatile("int $0x10" : "+a"(ax), "+b"(bx), "+d"(dx) :: "cc"); }

static void bar(const char *name, u16 peak, u16 *hold)
{
    char b[48]; int db = db_of(peak), n = db >= 60 ? 0 : (60 - db) / 2, i, h;
    if (n > *hold) *hold = (u16)n;
    h = *hold;
    for (i = 0; i < 30; i++) b[i] = i < n ? (i >= 27 ? '#' : '=') : (i == h - 1 ? '|' : '.');
    b[30] = 0;
    if (db >= 100) pr("%s [%s]  cisza   \n", name, b);
    else pr("%s [%s] -%2u dBFS%s\n", name, b, (u32)db, db == 0 ? " PRZESTER" : "        ");
}

static void meters(void)
{
    u16 row, hold[4] = {0, 0, 0, 0}, t0 = ticks(), th = ticks();
    pr("\nMierniki szczytowe (Esc - koniec, C - wyczysc nagranie, P - pauza/wznow):\n\n\n\n\n\n\n");
    cursor(&row); row -= 6;
    for (;;) {
        u16 p[4]; int i;
        while ((u16)(ticks() - t0) < 2) if (kbhit()) break;
        t0 = ticks();
        for (i = 0; i < 2; i++) {
            p[2 * i] = rdw(STO(i, peak)); p[2 * i + 1] = rdw(STO(i, peak) + 2);
            pokel(cseg, STO(i, peak), 0);
        }
        if ((u16)(ticks() - th) > 18) { th = ticks(); for (i = 0; i < 4; i++) if (hold[i]) hold[i]--; }
        gotoxy(row);
        bar("Wyjscie  L", p[0], &hold[0]); bar("Wyjscie  P", p[1], &hold[1]);
        bar("LINE IN  L", p[2], &hold[2]); bar("LINE IN  P", p[3], &hold[3]);
        {   u32 w0 = rd(STO(0, wr)), w1 = rd(STO(1, wr)), z0 = rd(STO(0, size)), z1 = rd(STO(1, size));
            if (w0 > z0) w0 = z0;
            if (w1 > z1) w1 = z1;
            pr("W buforze: wyjscie %u s, wejscie %u s (maks. %u s)%s     \n", w0 / 192000, w1 / 192000,
               (z0 > z1 ? z0 : z1) / 192000, (rdw(OFS(ctl)) & UPC_PAUSE) ? "  [PAUZA]            " :
               (rdw(OFS(flags)) & UPF_GATED) ? "  [czeka na dzwiek]" : "                     "); }
        if (kbhit()) {
            u16 k = getkey(); u8 ch = (u8)k;
            if (ch == 27 || ch == 'q' || ch == 'Q') break;
            if (ch == 'p' || ch == 'P') pokew(cseg, OFS(ctl), rdw(OFS(ctl)) ^ UPC_PAUSE);
            if (ch == 'c' || ch == 'C') {
                u16 c0 = rdw(OFS(ctl));
                pokew(cseg, OFS(ctl), c0 | UPC_PAUSE);
                for (i = 0; i < 2; i++) { pokel(cseg, STO(i, wr), 0); pokel(cseg, STO(i, lost), 0); }
                pokew(cseg, OFS(ctl), c0);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* zapis WAV                                                           */
/* ------------------------------------------------------------------ */
static u8 cbuf[16384];

static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static u32 amount(int st, u32 sec)
{
    u32 size = rd(STO(st, size)), wr = rd(STO(st, wr)), have = wr < size ? wr : size;
    if (sec && have > sec * 192000) have = sec * 192000;
    return have & ~3u;
}

static int save(int st, const char *name, u32 sec, u32 base)
{
    u32 size = rd(STO(st, size)), wr = rd(STO(st, wr)), phys = rd(STO(st, phys)), have, start, done = 0;
    u16 handle = rdw(OFS(xms_handle));
    u8 hdr[44];
    int h;
    have = wr < size ? wr : size;
    if (sec && have > sec * 192000) have = sec * 192000;
    have &= ~3u;
    if (!have) { pr("%s: brak danych (nic jeszcze nie nagrano)\n", name); return 0; }
    start = (wr - have) % size;
    memcpy(hdr, "RIFF....WAVEfmt ", 16);
    put32(hdr + 4, 36 + have); put32(hdr + 16, 16);
    hdr[20] = 1; hdr[21] = 0; hdr[22] = 2; hdr[23] = 0;
    put32(hdr + 24, 48000); put32(hdr + 28, 192000);
    hdr[32] = 4; hdr[33] = 0; hdr[34] = 16; hdr[35] = 0;
    memcpy(hdr + 36, "data", 4); put32(hdr + 40, have);
    h = dos_create(name);
    if (h < 0) { pr("%s: nie mozna utworzyc pliku\n", name); return 0; }
    if (dos_write((u16)h, hdr, 44) != 44) goto full;
    while (done < have) {
        u32 pos = (start + done) % size, n = have - done;
        if (n > sizeof cbuf) n = sizeof cbuf;
        if (n > size - pos) n = size - pos;
        if (!xms_to_low(handle, phys - base + pos, cbuf, n)) { pr("%s: blad kopiowania z XMS\n", name); dos_close(h); return 0; }
        if (dos_write((u16)h, cbuf, (u16)n) != (int)n) goto full;
        done += n;
    }
    dos_close(h);
    pr("%s: zapisano %u.%u s (%u KB)\n", name, have / 192000, (have % 192000) / 19200, (have + 44) / 1024);
    return 1;
full:
    dos_close(h);
    pr("%s: blad zapisu (pelny dysk?)\n", name);
    return 0;
}

static int write_files(u32 sec)
{
    u16 c0 = rdw(OFS(ctl)), fl = rdw(OFS(flags));
    u32 dx, bx = 0, base, ok = 0;
    if (!(fl & (UPF_REC_OUT | UPF_REC_IN))) { pr("Nagrywanie nie jest wlaczone. Przed SBEMU: SET USBPCM=R20\n"); return 1; }
    if (!xms_init()) { pr("BLAD: brak sterownika XMS\n"); return 1; }
    dx = rdw(OFS(xms_handle));
    if (xms_call(0x0C00, &dx, &bx) != 1) { pr("BLAD: XMS nie podal adresu bloku nagrania\n"); return 1; }
    base = (dx << 16) | bx;
    dx = rdw(OFS(xms_handle)); xms_call(0x0D00, &dx, 0);
    {   static const char *const grp[3] = { "RECOUT.WAV", "RECIN.WAV", "STAN.TXT" };
        u32 need = 4096, fr = rot_free_bytes();
        int n, h;
        if (fl & UPF_REC_OUT) need += amount(0, sec) + 44;
        if (fl & UPF_REC_IN) need += amount(1, sec) + 44;
        if (fr != 0xFFFFFFFFu && fr < need) {
            pr("Za malo miejsca na dysku: wolne %u KB, potrzeba %u KB.\n"
               "Skopiuj i usun stare nagrania (RECOUTnn.WAV, RECINnn.WAV) albo zapisz krocej: USBREC /W /S:10\n",
               fr / 1024, need / 1024);
            return 1;
        }
        n = rot_group(grp, 3);
        if (n > 0) pr("Poprzednie pliki zachowane z numerem %02u (RECOUT%02u.WAV, RECIN%02u.WAV, STAN%02u.TXT)\n", (u32)n, (u32)n, (u32)n, (u32)n);
        if (n < 0) { pr("BLAD: nie mozna zmienic nazw starych plikow (numery do 99 zajete?) - nic nie zapisano\n"); return 1; }
        h = dos_create("STAN.TXT");
        if (h >= 0) { outh = (u16)h; info(); outh = 1; dos_close(h); }
    }
    pokew(cseg, OFS(ctl), c0 | UPC_PAUSE);                 /* zamroz pierscienie na czas kopiowania */
    if (fl & UPF_REC_OUT) ok += save(0, "RECOUT.WAV", sec, base);
    if (fl & UPF_REC_IN) ok += save(1, "RECIN.WAV", sec, base);
    pokew(cseg, OFS(ctl), c0);
    if (ok) pr("Pliki WAV: 48000 Hz, 16 bit, stereo. Oba zaczynaja sie w tej samej chwili tylko w przyblizeniu\n"
               "(roznica = opoznienie toru LINE OUT -> LINE IN, okolo kilku ms).\n");
    return ok ? 0 : 1;
}

int main(void)
{
    char cmd[128]; int n = *(volatile u8 *)0x80, i;
    int o_w = 0, o_i = 0, o_c = 0, o_p = 0, o_g = 0;
    u32 sec = 0;
    const char *s;
    if (n > 126) n = 126;
    for (i = 0; i < n; i++) { char c = *(volatile char *)(0x81 + i); if (c >= 'a' && c <= 'z') c -= 32; cmd[i] = c; }
    cmd[n] = 0;
    for (s = cmd; *s; s++) {
        if (*s != '/' && *s != '-') continue;
        s++;
        if (*s == 'W') o_w = 1;
        else if (*s == 'I') o_i = 1;
        else if (*s == 'C') o_c = 1;
        else if (*s == 'P') o_p = 1;
        else if (*s == 'G') o_g = 1;
        else if (*s == 'S') { s++; if (*s == ':' || *s == '=') s++; while (*s >= '0' && *s <= '9') sec = sec * 10 + (u32)(*s++ - '0'); s--; }
        else if (*s == '?' || *s == 'H') {
            pr("USBREC - nagrania i mierniki sterownika USB Audio (SBEMU)\n"
               "  USBREC          stan i mierniki (Esc - koniec)\n"
               "  USBREC /I       tylko stan\n"
               "  USBREC /W       zapisz nagranie: RECOUT.WAV (wyjscie), RECIN.WAV (LINE IN)\n"
               "  USBREC /W /S:n  zapisz tylko ostatnie n sekund\n"
               "  USBREC /C       wyczysc nagranie   /P wstrzymaj   /G wznow\n"
               "Nagrywanie wlacza SET USBPCM=R20 (20 s) przed uruchomieniem SBEMU.\n");
            return 0;
        }
    }
    if (!find_block()) {
        pr("Nie znaleziono sterownika USB Audio. Czy SBEMU jest uruchomione z karta USB?\n");
        return 2;
    }
    if (o_p) { pokew(cseg, OFS(ctl), rdw(OFS(ctl)) | UPC_PAUSE); pr("Nagrywanie wstrzymane.\n"); }
    if (o_g) { pokew(cseg, OFS(ctl), rdw(OFS(ctl)) & ~UPC_PAUSE); pr("Nagrywanie wznowione.\n"); }
    if (o_c) {
        u16 c0 = rdw(OFS(ctl));
        pokew(cseg, OFS(ctl), c0 | UPC_PAUSE);
        for (i = 0; i < 2; i++) { pokel(cseg, STO(i, wr), 0); pokel(cseg, STO(i, lost), 0); }
        pokew(cseg, OFS(ctl), c0);
        pr("Nagranie wyczyszczone.\n");
    }
    if (o_w) return write_files(sec);   /* 0 = calosc (2 x 20 s = 7.5 MB, miesci sie na karcie 16 MB) */
    info();
    if (!o_i && !o_p && !o_g && !o_c) meters();
    return 0;
}
