/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * SBMIX.COM - test miksowania programowego na Sound Blasterze (licencja MIT).
 * Udaje gre, ktora sama miksuje muzyke i efekty w jeden strumien 8 bit mono:
 *   "muzyka"  = ciagly ton 440 Hz,
 *   "efekty"  = 2000 Hz przez 100 ms co 500 ms.
 * Kazda probka zalezy tylko od jej numeru, wiec idealny wynik jest znany.
 * Sposoby zapisu do bufora DMA (tak jak w grach):
 *   A  polowki bufora - miksowanie po kazdym przerwaniu (klasyczny odtwarzacz MOD)
 *   B  przed pozycja DMA - miksowanie tuz przed odczytanym licznikiem DMA
 *      z zapasem 100 ms, 20 ms i 5 ms
 * Wynik slychac (ton z pikaniem) i mozna go nagrac (USBREC /W) do analizy.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed short s16; typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end
#include "sine.h"
#include "usbpcmct.h"

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
    h = dos_open("SBMIX.LOG", !lcreated);
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
#define BUFSZ 8192
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

static int fails, fault, keepmix, latmode;   /* keepmix: /K - nie zmieniaj miksera */   /* fault: /F - celowy blad (test samej analizy) */
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
static int ask __attribute__((unused)) = 1;

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


/* ------------------------------------------------------------------ */
#define SIZE 4096
#define HALF (SIZE / 2)
static u32 rate, inc_m, inc_e, eper, elen;

static u8 smp(u32 n)
{
    s32 v = sine256[(u8)((n * inc_m) >> 24)] >> 8;               /* +-46 */
    if ((n % eper) < elen) v += sine256[(u8)((n * inc_e) >> 24)] >> 8;
    return (u8)(128 + v);
}
static u16 dma_pos(void)   /* pozycja odczytu w buforze (0..SIZE-1) */
{
    u16 c = dma8 & 3, a, b;
    do {
        outb(0x0C, 0); a = inb(c * 2 + 1); a |= (u16)inb(c * 2 + 1) << 8;
        outb(0x0C, 0); b = inb(c * 2 + 1); b |= (u16)inb(c * 2 + 1) << 8;
    } while (a != b && (u16)(a - b) > 4);
    return (u16)((SIZE - 1 - b) & (SIZE - 1));
}
static void start_ai(void)
{
    irq_count = 0;
    dma8_setup(SIZE, 1);
    dsp_write(0x40); dsp_write((u8)(256 - 1000000 / 22050));
    dsp_write(0x48); dsp_write((u8)(HALF - 1)); dsp_write((u8)((HALF - 1) >> 8));
    dsp_write(0x1C);
}
static void stop_ai(void)
{
    dsp_write(0xDA); wait_irqs(0xFFFF, 2); dsp_write(0xD0);
    dma_mask_all();
}
static void silence(u16 t) { u16 t0 = ticks(); while ((u16)(ticks() - t0) < t) ; }

#define RUN_TICKS 73   /* 4 s */
static void mark_start(void);

static void test_half(void)
{
    u32 nw, k; u16 last = 0, t0; int h = 0; u32 lost = 0;
    pr("[A] polowki bufora (miksowanie po przerwaniu), 4 s...\n");
    for (k = 0; k < SIZE; k++) buf[k] = smp(k);
    nw = SIZE;
    mark_start(); start_ai(); t0 = ticks();
    while ((u16)(ticks() - t0) < RUN_TICKS) {
        u16 ic = irq_count;
        if ((u16)(ic - last) > 1) lost += (u16)(ic - last) - 1;
        while (last != ic) {
            u8 *p = buf + h * HALF;
            if (!(fault && last % 3 == 1))
            for (k = 0; k < HALF; k++) p[k] = smp(nw + k);
            nw += HALF; h ^= 1; last++;
        }
        if (kbhit()) { getkey(); break; }
    }
    stop_ai();
    pr("    przerwan %u (oczekiwane ok. %u), zaleglosci %u\n", irq_count, (u32)(RUN_TICKS * 10 * rate / 182 / HALF), lost);
}

static void test_pos(u32 lead_ms)
{
    u32 lead = rate * lead_ms / 1000, nw, np, wraps = 0, k, under = 0, changes = 0, maxjump = 0, sumjump = 0;
    u16 prev = 0, t0;
    pr("[B] miksowanie przed pozycja DMA, zapas %u ms (%u probek), 4 s...\n", lead_ms, lead);
    for (k = 0; k < SIZE; k++) buf[k] = 128;
    for (k = 0; k < lead; k++) buf[k] = smp(k);
    nw = lead;
    mark_start(); start_ai(); t0 = ticks();
    while ((u16)(ticks() - t0) < RUN_TICKS) {
        u16 pos = dma_pos();
        if (pos != prev) {
            u32 j = (u16)(pos - prev) & (SIZE - 1);
            if (pos < prev) wraps += SIZE;
            changes++; sumjump += j; if (j > maxjump) maxjump = j;
            prev = pos;
        }
        np = wraps + pos;
        if (np > nw) { under++; nw = np; }
        while (nw < np + lead) { buf[nw & (SIZE - 1)] = smp(nw); nw++; }
        if (kbhit()) { getkey(); break; }
    }
    stop_ai();
    pr("    licznik DMA zmienil sie %u razy, sredni skok %u probek, najwiekszy %u probek (%u ms)\n",
       changes, changes ? sumjump / changes : 0, maxjump, maxjump * 1000 / rate);
    pr("    odtwarzanie wyprzedzilo zapis: %u razy\n", under);
}

/* ------------------------------------------------------------------ */
/* samodzielna analiza nagrania sterownika USB (blok "USBPCM01")        */
/* ------------------------------------------------------------------ */
static u32 peekl(u16 seg, u16 off) { u32 v; asm volatile("movw %1,%%fs\n\tmovl %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
#define OFS(f) ((u16)__builtin_offsetof(usbpcm_ctl_t, f))
#define STO(i, f) ((u16)(__builtin_offsetof(usbpcm_ctl_t, st) + (i) * sizeof(usbpcm_stream_t) + __builtin_offsetof(usbpcm_stream_t, f)))
static u16 cseg;
static int find_block(void)
{
    static const char sig[] = USBPCM_CTL_SIG;
    u16 sg, me = cs; int k;
    for (sg = 0x0050; sg < 0xA000; sg++) {
        if (sg >= me && sg < me + 0x1000) continue;
        for (k = 0; k < 8; k++) if (peekb(sg, (u16)k) != (u8)sig[k]) break;
        if (k < 8) continue;
        if (peekl(sg, OFS(self)) != (u32)sg * 16) continue;
        if (peekw(sg, OFS(version)) != USBPCM_CTL_VERSION) return 0;
        cseg = sg; return 1;
    }
    return 0;
}
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
static int xms_to_low(u16 handle, u32 off, void *dst, u32 len)
{
    u32 a = 0x0B00, b = 0;
    xmove.len = len; xmove.sh = handle; xmove.soff = off;
    xmove.dh = 0; xmove.doff = ((u32)cs << 16) | (u16)(u32)dst;
    asm volatile("lcallw *%2" : "+a"(a), "+b"(b) : "m"(xms_entry), "S"(&xmove) : "memory", "cc", "ecx", "edx", "edi");
    return (a & 0xFFFF) == 1;
}

static int an_ok;            /* analiza mozliwa */
static u32 xbase;            /* adres bazowy bloku XMS nagrania */
static u16 xhandle, an_flags;
#define WIN 480              /* 10 ms przy 48 kHz */
#define MAXW 600
static u32 w440[MAXW], w2k[MAXW];
static s16 abuf[WIN * 2 * 5];
static s16 cos440[WIN], sin440[WIN], cos2k[WIN], sin2k[WIN];

static void an_init(void)
{
    u32 dx, bx = 0, k, p, i4 = ((440u << 16) / 48000) << 16, i2 = ((2000u << 16) / 48000) << 16;
    an_ok = 0;
    if (!find_block()) { pr("(Analiza automatyczna niedostepna: brak sterownika USB SBEMU.)\n"); return; }
    an_flags = peekw(cseg, OFS(flags));
    if (!(an_flags & UPF_REC_OUT)) { pr("(Analiza automatyczna wymaga nagrywania: opcja 3 w menu, czyli SET USBPCM=R20.)\n"); return; }
    if (!xms_init()) return;
    xhandle = peekw(cseg, OFS(xms_handle));
    dx = xhandle;
    if (xms_call(0x0C00, &dx, &bx) != 1) { pr("(Analiza: XMS nie podal adresu nagrania)\n"); return; }
    xbase = (dx << 16) | bx;
    dx = xhandle; xms_call(0x0D00, &dx, 0);
    for (k = 0, p = 0; k < WIN; k++, p += i4) { cos440[k] = sine256[(u8)((p >> 24) + 64)] >> 4; sin440[k] = sine256[(u8)(p >> 24)] >> 4; }
    for (k = 0, p = 0; k < WIN; k++, p += i2) { cos2k[k] = sine256[(u8)((p >> 24) + 64)] >> 4; sin2k[k] = sine256[(u8)(p >> 24)] >> 4; }
    an_ok = 1;
    pr("Analiza automatyczna: wlaczona (nagranie sterownika: wyjscie%s)\n", (an_flags & UPF_REC_IN) ? " i LINE IN" : "");
}
static u32 mag(s32 i, s32 q) { u32 a = i < 0 ? -i : i, b = q < 0 ? -q : q; return a > b ? a + b / 2 : b + a / 2; }

/* okna 10 ms z pierscienia st od bajtu wr0 do wr1; zwraca liczbe okien */
static int an_read(int st, u32 wr0, u32 wr1)
{
    u32 size = peekl(cseg, STO(st, size)), phys = peekl(cseg, STO(st, phys)), wr = peekl(cseg, STO(st, wr));
    u32 n = (wr1 - wr0) / (WIN * 4), w, k;
    if (n > MAXW) { wr0 += (n - MAXW) * WIN * 4; n = MAXW; }
    if (wr - wr0 > size) return -1;                  /* juz nadpisane */
    for (w = 0; w < n; w += 5) {
        u32 nb = (n - w < 5 ? n - w : 5) * WIN * 4, pos = (wr0 + w * WIN * 4) % size, first = size - pos, j;
        if (first > nb) first = nb;
        if (!xms_to_low(xhandle, phys - xbase + pos, abuf, first)) return -1;
        if (nb > first && !xms_to_low(xhandle, phys - xbase, (u8 *)abuf + first, nb - first)) return -1;
        for (j = 0; j < nb / (WIN * 4); j++) {
            s32 a = 0, b = 0, c = 0, d = 0; s16 *x = abuf + j * WIN * 2;
            for (k = 0; k < WIN; k++) {
                s32 v = x[2 * k] >> 4;
                a += v * cos440[k]; b += v * sin440[k]; c += v * cos2k[k]; d += v * sin2k[k];
            }
            w440[w + j] = mag(a, b) >> 8; w2k[w + j] = mag(c, d) >> 8;
        }
    }
    return (int)n;
}
static u32 median(const u32 *v, int a, int b)
{
    static u32 t[MAXW]; int i, j, n = 0;
    for (i = a; i < b; i++) { u32 x = v[i]; for (j = n; j > 0 && t[j - 1] > x; j--) t[j] = t[j - 1]; t[j] = x; n++; }
    return n ? t[n / 2] : 0;
}
/* 0 = OK, 1 = blad */
static int an_seg(int st, u32 wr0, u32 wr1, const char *what)
{
    int n = an_read(st, wr0, wr1), a, b, i, edge = 0, bursts = 0, badlen = 0, badgap = 0, gaps = 0, last = -1, inb = 0, blen = 0, err = 0;
    u32 mmax = 0, M, E = 0, thr, mon = 0, moff = 0, non = 0, noff = 0, under;
    if (n < 0) { pr("    %s: nagranie juz nadpisane - nie mozna sprawdzic\n", what); return 1; }
    for (i = 0; i < n; i++) if (w440[i] > mmax) mmax = w440[i];
    if (mmax < 1300) { pr("    %s: brak sygnalu%s\n", what, st ? " (czy kabel LINE OUT -> LINE IN jest podlaczony?)" : ""); return 1; }
    for (a = 0; a < n && w440[a] < mmax / 4; a++) ;
    for (b = n; b > a && w440[b - 1] < mmax / 4; b--) ;
    a += 3; b -= 3;                                   /* brzegi (start/stop DMA) pomijamy */
    if (b - a < 100) { pr("    %s: za krotkie nagranie (%u ms)\n", what, (u32)(b - a) * 10); return 1; }
    M = median(w440, a, b);
    for (i = a; i < b; i++) if (w2k[i] > E) E = w2k[i];
    thr = E / 2;
    for (i = a; i < b; i++) if (w440[i] < M / 2) gaps++;
    for (i = a; i <= b; i++) {
        int on = i < b && w2k[i] > thr;
        if (i < b) { if (on) { mon += w440[i]; non++; } else { moff += w440[i]; noff++; } }
        if (on && !inb) { if (last >= 0) { int g = i - last; if (g < 47 || g > 53) badgap++; } last = i; inb = 1; blen = 1; edge = (i == a); }
        else if (on) blen++;
        else if (inb) { inb = 0; if (i < b) { bursts++; if (!edge && (blen < 8 || blen > 12)) badlen++; } }
    }
    under = (non && noff && moff) ? (mon / non) * 100 / (moff / noff) : 0;
    pr("    %s: muzyka %u ms, przerw %u, efektow %u (zla dlugosc %u, zly odstep %u), muzyka pod efektem %u%%\n",
       what, (u32)(b - a) * 10, (u32)gaps, (u32)bursts, (u32)badlen, (u32)badgap, under);
    if (gaps > 1) { pr("      BLAD: muzyka sie rwie\n"); err = 1; }
    if (bursts < (b - a) / 50 - 1) { pr("      BLAD: brakuje efektow\n"); err = 1; }
    if (badlen > 1 || badgap > 1) { pr("      BLAD: efekty uciete, przesuniete albo powtorzone\n"); err = 1; }
    if (under < 70) { pr("      BLAD: muzyka cichnie pod efektem\n"); err = 1; }
    if (!err) pr("      OK\n");
    return err;
}
static u32 mk0, mk1;
static void mark_start(void) { if (an_ok) { mk0 = peekl(cseg, STO(0, wr)); mk1 = peekl(cseg, STO(1, wr)); } }
static int fails_out, fails_in, checked, stress_out, stress_in, stress;
static void mark_end_analyse(void)
{
    if (!an_ok) return;
    checked++;
    {   int e = an_seg(0, mk0, peekl(cseg, STO(0, wr)), "wyjscie (to, co gra SBEMU)");
        if (stress) stress_out += e; else fails_out += e; }
    if (an_flags & UPF_REC_IN) {
        int e = an_seg(1, mk1, peekl(cseg, STO(1, wr)), "LINE IN (to, co wyszlo z karty)");
        if (stress) stress_in += e; else fails_in += e; }
}

/* ------------------------------------------------------------------ */
/* /L: opoznienie od komendy DSP do dzwieku w strumieniu USB / na LINE IN */
/* ------------------------------------------------------------------ */
static int find_onset(int st, u32 wr0, u32 *ms100)
{
    u32 size = peekl(cseg, STO(st, size)), phys = peekl(cseg, STO(st, phys)), wr = peekl(cseg, STO(st, wr));
    u32 done = 0, avail = wr - wr0, k;
    if (!size || avail > size) return 0;
    if (avail > 48000 * 4) avail = 48000 * 4;                 /* szukaj do 1 s */
    while (done < avail) {
        u32 pos = (wr0 + done) % size, n = avail - done, first;
        if (n > sizeof abuf) n = sizeof abuf;
        first = size - pos; if (first > n) first = n;
        if (!xms_to_low(xhandle, phys - xbase + pos, abuf, first)) return 0;
        if (n > first && !xms_to_low(xhandle, phys - xbase, (u8 *)abuf + first, n - first)) return 0;
        for (k = 0; k < n / 4; k++) {
            s16 v = abuf[2 * k]; if (v < 0) v = -v;
            if (v > 3000) { *ms100 = (done / 4 + k) * 100 / 48; return 1; }   /* setne ms */
        }
        done += n;
    }
    return 0;
}
static void test_latency(void)
{
    u32 n = 444, k, i, cnt[2] = {0, 0}, mn[2] = {~0u, ~0u}, mx[2] = {0, 0}, sum[2] = {0, 0}, inc = ((1000u << 16) / 22222) << 16, p = 0;
    u16 c0 = peekw(cseg, OFS(ctl));
    pr("[L] opoznienie: komenda DSP (start DMA) -> dzwiek wyslany przez USB%s; 10 pomiarow\n",
       (an_flags & UPF_REC_IN) ? " i -> dzwiek na LINE IN (kabel)" : "");
    pr("    bufor sterownika: %u ms (zmiana: SET USBPCM=Lnn przed SBEMU, 16-128)\n", (u32)peekw(cseg, OFS(ring_ms)));
    for (k = 0; k < n; k++) { buf[k] = (u8)(128 + (sine256[(u8)(p >> 24)] * 100 / 12000)); p += inc; }
    pokew(cseg, OFS(ctl), (u16)((c0 | UPC_NOGATE) & ~UPC_PAUSE));
    if (type >= 6) { mix_wr(0x30, 0xF8); mix_wr(0x31, 0xF8); mix_wr(0x32, 0xF8); mix_wr(0x33, 0xF8); }
    for (i = 0; i < 10; i++) {
        u32 w0, w1, ms;
        silence(5);
        w0 = peekl(cseg, STO(0, wr)); w1 = peekl(cseg, STO(1, wr));
        dma8_setup(n, 0);
        dsp_write(0x40); dsp_write((u8)(256 - 1000000 / 22050));
        dsp_write(0x14); dsp_write((u8)(n - 1)); dsp_write((u8)((n - 1) >> 8));
        silence(10);
        pr("    %2u:", i + 1);
        if (find_onset(0, w0, &ms)) { pr(" USB %3u.%02u ms", ms / 100, ms % 100); cnt[0]++; sum[0] += ms; if (ms < mn[0]) mn[0] = ms; if (ms > mx[0]) mx[0] = ms; }
        else pr(" USB    brak  ");
        if (an_flags & UPF_REC_IN) {
            if (find_onset(1, w1, &ms)) { pr("   LINE IN %3u.%02u ms", ms / 100, ms % 100); cnt[1]++; sum[1] += ms; if (ms < mn[1]) mn[1] = ms; if (ms > mx[1]) mx[1] = ms; }
            else pr("   LINE IN brak");
        }
        pr("\n");
    }
    pokew(cseg, OFS(ctl), c0);
    for (i = 0; i < 2; i++) if (cnt[i])
        pr("WYNIK %s: srednio %u.%u ms (od %u.%u do %u.%u ms)\n", i ? "LINE IN (z przetwornikami)" : "wyslanie przez USB",
           sum[i] / cnt[i] / 100, sum[i] / cnt[i] % 100 / 10, mn[i] / 100, mn[i] % 100 / 10, mx[i] / 100, mx[i] % 100 / 10);
    pr("Do tego dochodzi bufor samej gry (zalezy od gry) i odczyt klawiatury.\n");
}

int main(void)
{
    {   int len = *(volatile u8 *)0x80, i;
        for (i = 0; i < len; i++) { char ch = *(volatile char *)(0x81 + i) | 32; if (ch == 'f') fault = 1; if (ch == 'k') keepmix = 1; if (ch == 'l') latmode = 1; } }
    asm volatile("movw %%cs,%0" : "=r"(cs));
    seg_lin = (u32)cs << 4;
    pr("SBMIX 0.1 - test miksowania programowego (muzyka 440 Hz + efekty 2000 Hz), log: SBMIX.LOG\n");
    parse_blaster();
    pr("Ustawienia: port %x, IRQ %u, DMA %u, typ %u\n", base, irq, dma8, type);
    setup_buffer();
    if (!dsp_reset()) { pr("BLAD: brak odpowiedzi DSP (czy SBEMU jest zaladowany?)\n"); return 1; }
    rate = 1000000 / (256 - (256 - 1000000 / 22050));      /* rzeczywista: 22222 Hz */
    inc_m = ((440u << 16) / rate) << 16;
    inc_e = ((2000u << 16) / rate) << 16;
    eper = rate / 2; elen = rate / 10;
    pr("Probkowanie %u Hz, bufor DMA %u B, przerwanie co %u B\n", rate, (u32)SIZE, (u32)HALF);
    if (type >= 6 && !keepmix) {   /* glosnosc: master 0 dB, voice 13/31 = -36 dB (SB16: 2 dB na krok) - petla
                          LINE OUT -> LINE IN wzmacnia o 23-29 dB i bez tego nagranie z LINE IN
                          byloby przesterowane */
        mix_wr(0x30, 0xF8); mix_wr(0x31, 0xF8); mix_wr(0x32, 0x68); mix_wr(0x33, 0x68);
    }
    an_init();
    if (an_ok) { u16 c = peekw(cseg, OFS(ctl)); if (c & UPC_PAUSE) pokew(cseg, OFS(ctl), c & ~UPC_PAUSE); }
    hook_irq();
    dsp_write(0xD1);
    if (latmode) {
        if (!an_ok) pr("Pomiar opoznienia wymaga nagrywania: opcja 3 w menu (SET USBPCM=R20).\n");
        else test_latency();
        unhook_irq();
        return 0;
    }
    test_half();  silence(4); mark_end_analyse(); silence(4);
    test_pos(100); silence(4); mark_end_analyse(); silence(4);
    test_pos(20);  silence(4); mark_end_analyse(); silence(4);
    stress = 1;
    test_pos(5);   silence(4); mark_end_analyse();
    unhook_irq();
    if (type >= 6 && !keepmix) { mix_wr(0x30, 0xF8); mix_wr(0x31, 0xF8); mix_wr(0x32, 0xF8); mix_wr(0x33, 0xF8); }
    if (an_ok && checked == 4) {
        pr("WYNIK (wyjscie SBEMU, sposoby A, B 100 ms, B 20 ms): %s\n", fails_out ? "BLEDY - szczegoly wyzej" : "OK - muzyka i efekty mieszaja sie poprawnie");
        pr("  test wytrzymalosci (B 5 ms - bardzo maly zapas): %s\n", stress_out ? "nie przeszedl" : "OK");
        if (an_flags & UPF_REC_IN)
            pr("WYNIK (LINE IN, przez kabel): %s\n", fails_in ? "BLEDY - szczegoly wyzej (bez kabla to normalne)" : "OK - karta odtwarza to samo");
    } else
        pr("Koniec. Do analizy: opcja 3 w menu (nagrywanie), potem SBMIX jeszcze raz.\n");
    return fails_out ? 3 : 0;
}
