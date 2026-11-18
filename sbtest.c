/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * SBTEST.COM - test emulowanego (lub prawdziwego) Sound Blastera w DOS.
 * Dziala w trybie rzeczywistym i V86 (pod JEMMEX/EMM386). Licencja: MIT.
 *
 * Kazdy krok jest zapisywany do SBTEST.LOG ZANIM sie zacznie.
 * Testy:
 *  1. BLASTER, reset DSP, wersja DSP
 *  2. mikser (zapis/odczyt)
 *  3. przerwanie na zadanie (DSP F2h / F3h)
 *  4. 8 bit mono, pojedynczy transfer DMA (14h), 11025 Hz - ton 440 Hz
 *  5. 8 bit mono auto-init (1Ch), 22050 Hz, 2 s - ton 660 Hz, licznik IRQ
 *  6. 8 bit stereo SB Pro (mikser 0Eh + 1Ch), 22050 Hz, 2 s - 550 Hz lewy, 825 Hz prawy
 *  7. 16 bit stereo SB16 auto-init (B6h), 44100 Hz, 2 s - 880 Hz
 *  8. 16 bit mono SB16 auto-init (B6h), 22050 Hz, 2 s - 1100 Hz
 *  9. 8 bit "direct" (komenda 10h) - 0.5 s tonu 330 Hz
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed short s16; typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end
#include "sine.h"

void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

/* dostep do innych segmentow (dziala tez w V86) */
static u8 peekb(u16 seg, u16 off) { u8 v; asm volatile("movw %1,%%fs\n\tmovb %%fs:(%2),%0" : "=q"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u16 peekw(u16 seg, u16 off) { u16 v; asm volatile("movw %1,%%fs\n\tmovw %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static void pokew(u16 seg, u16 off, u16 v) { asm volatile("movw %0,%%fs\n\tmovw %1,%%fs:(%2)" :: "r"(seg), "r"(v), "r"((u32)off) : "memory"); }
static u16 ticks(void) { return peekw(0x40, 0x6C); }

static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_open(const char *n, int create)
{ u32 ax = create ? 0x3C00 : 0x3D02; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static void dos_seek_end(int h) { u32 ax = 0x4202, cx = 0, dx = 0; asm volatile("int $0x21" : "+a"(ax), "+c"(cx), "+d"(dx) : "b"((u32)h) : "memory", "cc"); }
static int kbhit(void) { u32 ax = 0x0100; u8 nz; asm volatile("int $0x16; setnz %1" : "+a"(ax), "=qm"(nz) :: "memory", "cc"); return nz; }
static u16 getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }

static char lbuf[600]; static int llen, lcreated;
static void lflush(void)
{
    int h; if (!llen) return;
    h = dos_open("SBTEST.LOG", !lcreated);
    if (h >= 0) { if (lcreated) dos_seek_end(h); dos_write((u16)h, lbuf, (u16)llen); dos_close(h); }
    lcreated = 1; llen = 0;
}
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
    char b[256]; va_list ap; int n, i;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    for (i = 0; i < n; i++) {
        if (b[i] == '\n') { dos_write(1, "\r\n", 2); if (llen < 590) { lbuf[llen++] = '\r'; lbuf[llen++] = '\n'; } }
        else { dos_write(1, &b[i], 1); if (llen < 598) lbuf[llen++] = b[i]; }
    }
    if (n && b[n - 1] == '\n') lflush();
}

/* ------------------------------------------------------------------ */
/* obsluga przerwania karty                                            */
/* ------------------------------------------------------------------ */
extern u16 sb_port_ack8, sb_port_ack16, irq_count, irq_hi;
extern void sb_isr(void);
asm(
    ".code16\n"
    ".globl sb_isr, sb_port_ack8, sb_port_ack16, irq_count, irq_hi\n"
    "sb_port_ack8:  .word 0x22E\n"
    "sb_port_ack16: .word 0x22F\n"
    "irq_count:     .word 0\n"
    "irq_hi:        .word 0\n"
    "sb_isr:\n"
    "  pushw %ax\n  pushw %dx\n"
    "  incw %cs:irq_count\n"
    "  movw %cs:sb_port_ack8,%dx\n  inb %dx,%al\n"
    "  movw %cs:sb_port_ack16,%dx\n  inb %dx,%al\n"
    "  movb $0x20,%al\n"
    "  cmpw $0,%cs:irq_hi\n  je 1f\n  outb %al,$0xA0\n"
    "1: outb %al,$0x20\n"
    "  popw %dx\n  popw %ax\n  iret\n"
    ".code16gcc\n");

static u16 base = 0x220, irq = 5, dma8 = 1, dma16 = 5, type = 6;
static u16 cs;
static u32 seg_lin;
static u16 old_off, old_seg, vec;

static void hook_irq(void)
{
    vec = irq < 8 ? 8 + irq : 0x70 + irq - 8;
    irq_hi = irq >= 8;
    sb_port_ack8 = base + 0xE; sb_port_ack16 = base + 0xF;
    asm volatile("cli");
    old_off = peekw(0, vec * 4); old_seg = peekw(0, vec * 4 + 2);
    pokew(0, vec * 4, (u16)(u32)sb_isr); pokew(0, vec * 4 + 2, cs);
    if (irq < 8) outb(0x21, inb(0x21) & ~(1 << irq));
    else { outb(0xA1, inb(0xA1) & ~(1 << (irq - 8))); outb(0x21, inb(0x21) & ~4); }
    asm volatile("sti");
}
static void unhook_irq(void)
{
    asm volatile("cli");
    pokew(0, vec * 4, old_off); pokew(0, vec * 4 + 2, old_seg);
    asm volatile("sti");
}

/* ------------------------------------------------------------------ */
/* DSP                                                                 */
/* ------------------------------------------------------------------ */
static int dsp_write(u8 v)
{
    u32 i;
    for (i = 0; i < 100000; i++) if (!(inb(base + 0xC) & 0x80)) { outb(base + 0xC, v); return 1; }
    return 0;
}
static int dsp_read(void)
{
    u32 i;
    for (i = 0; i < 100000; i++) if (inb(base + 0xE) & 0x80) return inb(base + 0xA);
    return -1;
}
static int dsp_reset(void)
{
    int i;
    outb(base + 6, 1);
    for (i = 0; i < 16; i++) inb(base + 6);   /* > 3 us */
    outb(base + 6, 0);
    for (i = 0; i < 1000; i++) if (dsp_read() == 0xAA) return 1;
    return 0;
}
static void mix_wr(u8 r, u8 v) { outb(base + 4, r); outb(base + 5, v); }
static u8 mix_rd(u8 r) { outb(base + 4, r); return inb(base + 5); }

/* ------------------------------------------------------------------ */
/* bufor DMA (w naszym segmencie, nie przekracza granicy 64 KB)         */
/* ------------------------------------------------------------------ */
#define BUFSZ 18000
static u8 dmapool[BUFSZ * 2 + 16];
static u8 *buf; static u32 buf_lin;

static void setup_buffer(void)
{
    u32 lin = seg_lin + (u32)dmapool;
    lin = (lin + 15) & ~15u;
    if (((lin & 0xFFFF) + BUFSZ) > 0x10000) lin = (lin + 0xFFFF) & ~0xFFFFu;
    if (((lin & 0x1FFFF) + BUFSZ) > 0x20000) lin = (lin + 0x1FFFF) & ~0x1FFFFu;
    buf_lin = lin;
    buf = (u8 *)(lin - seg_lin);
}
static void fill8(u32 n, int ch, u32 hzL, u32 hzR, u32 rate)
{
    u32 pL = 0, pR = 0, iL = ((hzL << 16) / rate) << 16, iR = ((hzR << 16) / rate) << 16, k;
    for (k = 0; k < n; k++) {
        buf[k * ch] = (u8)(128 + (sine256[pL >> 24] >> 8)); pL += iL;
        if (ch == 2) { buf[k * 2 + 1] = (u8)(128 + (sine256[pR >> 24] >> 8)); pR += iR; }
    }
}
static void fill16(u32 n, int ch, u32 hz, u32 rate)
{
    s16 *b = (s16 *)buf; u32 p = 0, inc = ((hz << 16) / rate) << 16, k;
    for (k = 0; k < n; k++) { s16 s = sine256[p >> 24]; p += inc; b[k * ch] = s; if (ch == 2) b[k * 2 + 1] = s; }
}

static void dma8_setup(u32 bytes, int autoinit)
{
    static const u8 page[4] = { 0x87, 0x83, 0x81, 0x82 };
    u16 c = dma8 & 3;
    outb(0x0A, 4 | c);
    outb(0x0C, 0);
    outb(0x0B, (autoinit ? 0x58 : 0x48) | c);
    outb(c * 2, (u8)buf_lin); outb(c * 2, (u8)(buf_lin >> 8));
    outb(page[c], (u8)(buf_lin >> 16));
    outb(c * 2 + 1, (u8)(bytes - 1)); outb(c * 2 + 1, (u8)((bytes - 1) >> 8));
    outb(0x0A, c);
}
static void dma16_setup(u32 bytes, int autoinit)
{
    static const u8 page[4] = { 0, 0x8B, 0x89, 0x8A };
    u16 c = dma16 & 3; u32 w = bytes / 2, a = (buf_lin >> 1) & 0xFFFF;
    outb(0xD4, 4 | c);
    outb(0xD8, 0);
    outb(0xD6, (autoinit ? 0x58 : 0x48) | c);
    outb(0xC0 + c * 4, (u8)a); outb(0xC0 + c * 4, (u8)(a >> 8));
    outb(page[c], (u8)((buf_lin >> 16) & 0xFE));
    outb(0xC2 + c * 4, (u8)(w - 1)); outb(0xC2 + c * 4, (u8)((w - 1) >> 8));
    outb(0xD4, c);
}
static void dma_mask_all(void) { outb(0x0A, 4 | (dma8 & 3)); outb(0xD4, 4 | (dma16 & 3)); }

static int fails;
static void result(int ok, const char *what) { pr("    -> %s %s\n", ok ? "OK  " : "BLAD", what); if (!ok) fails++; }

static u16 wait_irqs(u16 target, u16 maxticks)
{
    u16 t = ticks();
    while (irq_count < target && (u16)(ticks() - t) < maxticks) if (kbhit()) { getkey(); break; }
    return (u16)(ticks() - t);
}

static void ask_heard(void)
{
    pr("    Czy slychac bylo ton? (T/N, inny klawisz = pomin) ");
    { u16 k = getkey() & 0xFF; pr("%c\n", (k >= 32 && k < 127) ? k : '-');
      if (k == 'n' || k == 'N') { pr("    -> uzytkownik: NIE slychac\n"); fails++; }
      else if (k == 't' || k == 'T') pr("    -> uzytkownik: slychac\n"); }
}
static int ask = 1;

/* ------------------------------------------------------------------ */
static void parse_blaster(void)
{
    u16 envseg = *(volatile u16 *)0x2C, o = 0;
    while (o < 4096) {
        char name[8]; int i;
        if (!peekb(envseg, o)) break;
        for (i = 0; i < 8; i++) name[i] = (char)peekb(envseg, o + i);
        if (name[0] == 'B' && name[1] == 'L' && name[2] == 'A' && name[3] == 'S' && name[4] == 'T' && name[5] == 'E' && name[6] == 'R' && name[7] == '=') {
            u16 p = o + 8;
            pr("BLASTER=");
            while (peekb(envseg, p)) {
                char c = (char)peekb(envseg, p++), d; u16 v = 0, hex = (c == 'A' || c == 'a' || c == 'P' || c == 'p');
                pr("%c", c);
                while ((d = (char)peekb(envseg, p)) && d != ' ') {
                    pr("%c", d);
                    if (d >= '0' && d <= '9') v = v * (hex ? 16 : 10) + (d - '0');
                    else if (hex && ((d | 32) >= 'a' && (d | 32) <= 'f')) v = v * 16 + ((d | 32) - 'a' + 10);
                    p++;
                }
                if (d == ' ') { pr(" "); p++; }
                switch (c | 32) { case 'a': base = v; break; case 'i': irq = v; break; case 'd': dma8 = v; break;
                                  case 'h': dma16 = v; break; case 't': type = v; break; }
            }
            pr("\n");
            return;
        }
        while (peekb(envseg, o)) o++;
        o++;
    }
    pr("Brak zmiennej BLASTER - przyjmuje A220 I5 D1 H5 T6\n");
}

int main(void)
{
    int ver_hi = 0, ver_lo = 0;
    u16 dt; u32 n;
    {   /* opcja /N - bez pytan */
        int len = *(volatile u8 *)0x80, i;
        for (i = 0; i < len; i++) { char c = *(volatile char *)(0x81 + i); if ((c | 32) == 'n') ask = 0; } }
    asm volatile("movw %%cs,%0" : "=r"(cs));
    seg_lin = (u32)cs << 4;
    pr("SBTEST 0.1 - test Sound Blastera (log: SBTEST.LOG). ESC/klawisz przerywa dany test.\n");
    parse_blaster();
    pr("Ustawienia: port %x, IRQ %u, DMA %u, HDMA %u, typ %u\n", base, irq, dma8, dma16, type);
    setup_buffer();
    pr("Bufor DMA: adres fizyczny %x\n", buf_lin);

    pr("[1] Reset DSP na porcie %x...\n", base);
    if (!dsp_reset()) { result(0, "brak odpowiedzi DSP (czy SBEMU jest zaladowany?)"); return 1; }
    result(1, "DSP odpowiada (AAh)");
    dsp_write(0xE1); ver_hi = dsp_read(); ver_lo = dsp_read();
    pr("    wersja DSP: %u.%02u\n", ver_hi, ver_lo);
    result(ver_hi >= 1, "odczyt wersji");

    pr("[2] Mikser: zapis/odczyt rejestru 22h (glosnosc glowna)...\n");
    { u8 old = mix_rd(0x22), v; mix_wr(0x22, 0x99); v = mix_rd(0x22); mix_wr(0x22, 0xFF);
      pr("    zapisano 99h, odczytano %x (bylo %x)\n", v, old); result(v == 0x99, "mikser"); }
    if (ver_hi >= 4) { mix_wr(0x30, 0xF8); mix_wr(0x31, 0xF8); mix_wr(0x32, 0xF8); mix_wr(0x33, 0xF8); }

    hook_irq();
    pr("[3] Przerwanie na zadanie (DSP F2h), IRQ %u...\n", irq);
    irq_count = 0; dsp_write(0xF2); dt = wait_irqs(1, 9);
    pr("    przerwan: %u\n", irq_count); result(irq_count == 1, "IRQ 8-bit");
    if (ver_hi >= 4) { irq_count = 0; dsp_write(0xF3); wait_irqs(1, 9); pr("    F3h przerwan: %u\n", irq_count); result(irq_count == 1, "IRQ 16-bit"); }

    dsp_write(0xD1);   /* glosnik wlaczony */

    pr("[4] 8 bit mono, pojedynczy DMA (14h), 11025 Hz, 1 s, ton 440 Hz...\n");
    n = 11025; if (n > BUFSZ) n = BUFSZ;
    fill8(n, 1, 440, 440, 11025);
    irq_count = 0; dma8_setup(n, 0);
    dsp_write(0x40); dsp_write((u8)(256 - 1000000 / 11025));
    dsp_write(0x14); dsp_write((u8)(n - 1)); dsp_write((u8)((n - 1) >> 8));
    dt = wait_irqs(1, 60);
    pr("    IRQ po %u tykach (oczekiwane ok. %u), przerwan %u\n", dt, (n * 182) / (11025 * 10), irq_count);
    result(irq_count == 1 && dt >= 13 && dt <= 26, "czas odtwarzania ~1 s");
    if (ask) ask_heard();

    pr("[5] 8 bit mono auto-init (1Ch), 22050 Hz, 2 s, ton 660 Hz, blok 2205 B...\n");
    fill8(BUFSZ, 1, 660, 660, 22050);
    { u32 blk = 2205, tot = blk * 2; u16 exp_ = 20;   /* 2 s / 0.1 s */
      fill8(tot, 1, 660, 660, 22050);
      irq_count = 0; dma8_setup(tot, 1);
      dsp_write(0x40); dsp_write((u8)(256 - 1000000 / 22050));
      dsp_write(0x48); dsp_write((u8)(blk - 1)); dsp_write((u8)((blk - 1) >> 8));
      dsp_write(0x1C);
      dt = wait_irqs(exp_, 60);
      dsp_write(0xDA); wait_irqs(0xFFFF, 3); dsp_write(0xD0);
      pr("    %u przerwan w %u tykach (oczekiwane %u w ok. 36)\n", irq_count, dt, exp_);
      result(irq_count >= exp_ && dt >= 30 && dt <= 44, "auto-init 8 bit"); }
    if (ask) ask_heard();

    if (ver_hi >= 3) {
        pr("[6] 8 bit STEREO (SB Pro), 22050 Hz, 2 s: 550 Hz (nizszy) LEWY, 825 Hz PRAWY...\n");
        { u32 blk = 4410, tot = blk * 2; u16 exp_ = 20;
          fill8(tot / 2, 2, 550, 825, 22050);
          mix_wr(0x0E, mix_rd(0x0E) | 2);
          irq_count = 0; dma8_setup(tot, 1);
          dsp_write(0xD1);
          dsp_write(0x40); dsp_write((u8)(256 - 1000000 / 44100));
          dsp_write(0x48); dsp_write((u8)(blk - 1)); dsp_write((u8)((blk - 1) >> 8));
          dsp_write(0x1C);
          dt = wait_irqs(exp_, 60);
          dsp_write(0xDA); wait_irqs(0xFFFF, 3); dsp_write(0xD0);
          mix_wr(0x0E, mix_rd(0x0E) & ~2);
          pr("    %u przerwan w %u tykach (oczekiwane %u w ok. 36)\n", irq_count, dt, exp_);
          result(irq_count >= exp_ && dt >= 30 && dt <= 44, "8 bit stereo"); }
        if (ask) ask_heard();
    }

    if (ver_hi >= 4) {
        pr("[7] 16 bit STEREO SB16 (B6h), 44100 Hz, 2 s, ton 880 Hz...\n");
        { u32 frames_blk = 2205, tot = frames_blk * 2 * 4 /* 2 bloki, stereo 16 bit */; u16 exp_ = 40;
          fill16(frames_blk * 2, 2, 880, 44100);
          irq_count = 0; dma16_setup(tot, 1);
          dsp_write(0x41); dsp_write(44100 >> 8); dsp_write(44100 & 0xFF);
          dsp_write(0xB6); dsp_write(0x30);
          { u32 w = frames_blk * 2 - 1; dsp_write((u8)w); dsp_write((u8)(w >> 8)); }
          dt = wait_irqs(exp_, 60);
          dsp_write(0xD9); wait_irqs(0xFFFF, 3); dsp_write(0xD5);
          pr("    %u przerwan w %u tykach (oczekiwane %u w ok. 36)\n", irq_count, dt, exp_);
          result(irq_count >= exp_ && dt >= 30 && dt <= 44, "16 bit stereo (oczekiwane 40 przerwan)"); }
        if (ask) ask_heard();

        pr("[8] 16 bit MONO SB16 (B6h), 22050 Hz, 2 s, ton 1100 Hz...\n");
        { u32 blk = 2205, tot = blk * 2 * 2; u16 exp_ = 20;
          fill16(blk * 2, 1, 1100, 22050);
          irq_count = 0; dma16_setup(tot, 1);
          dsp_write(0x41); dsp_write(22050 >> 8); dsp_write(22050 & 0xFF);
          dsp_write(0xB6); dsp_write(0x10);
          { u32 w = blk - 1; dsp_write((u8)w); dsp_write((u8)(w >> 8)); }
          dt = wait_irqs(exp_, 60);
          dsp_write(0xD9); wait_irqs(0xFFFF, 3); dsp_write(0xD5);
          pr("    %u przerwan w %u tykach (oczekiwane %u w ok. 36)\n", irq_count, dt, exp_);
          result(irq_count >= exp_ && dt >= 30 && dt <= 44, "16 bit mono"); }
        if (ask) ask_heard();
    }

    pr("[9] Tryb bezposredni (10h), ok. 0.5 s, ton 330 Hz...\n");
    {   /* probki co ~45 us mierzone licznikiem PIT nie sa dostepne w V86 - petla z odczytem portu */
        u32 p = 0, inc = ((330u << 16) / 11025) << 16, k; u16 t0 = ticks();
        dsp_write(0xD1);
        for (k = 0; k < 5500 && (u16)(ticks() - t0) < 20; k++) {
            int d; dsp_write(0x10); dsp_write((u8)(128 + (sine256[p >> 24] >> 8))); p += inc;
            for (d = 0; d < 40; d++) inb(0x80);
        }
        pr("    wyslano %u probek w %u tykach\n", k, (u16)(ticks() - t0));
        result(1, "(ocena tylko na sluch)");
        if (ask) ask_heard();
    }

    dma_mask_all();
    unhook_irq();
    dsp_reset();
    pr("Koniec. Bledow: %u. Prosze przeslac SBTEST.LOG\n", fails);
    lflush();
    return fails ? 1 : 0;
}
