/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * CPUTEST.COM - diagnostyka procesora pod katem SBEMU / emulacji SB16
 * (eBOX-2300SX, Vortex86SX bez FPU). Licencja: MIT.
 *
 * Kazdy krok jest zapisywany do CPUTEST.LOG ZANIM zostanie wykonany,
 * wiec jesli komputer sie zawiesi, ostatnia linia logu pokazuje winowajce.
 *
 * Testy:
 *  1. tryb procesora (V86?), CR0 (bity EM/MP/ET/NE), CPUID
 *  2. pamiec konwencjonalna, XMS, A20
 *  3. pulapka FPU: CR0.EM=1 -> czy instrukcja FPU wywoluje przerwanie 7
 *     (warunek, by programowa emulacja FPU w ogole byla mozliwa)
 *  4. klasyczne wykrywanie koprocesora (FNINIT/FNSTSW) przy EM=0
 *  5. wykonanie FLD1/FADD/FISTP przy EM=0 - co robi procesor bez FPU
 *  6. wydajnosc: przeliczanie i miksowanie dzwieku (jak w emulatorze SB)
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed short s16; typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end

void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

static u32 seg_base;
static u8 gdt[16] __attribute__((aligned(8))) = { 0,0,0,0,0,0,0,0, 0xFF,0xFF,0,0,0,0x92,0xCF,0 };
static struct { u16 lim; u32 base; } __attribute__((packed)) gdtr;
static void unreal_fs(void)
{
    asm volatile("pushfl\n cli\n lgdtl %0\n movl %%cr0,%%eax\n orb $1,%%al\n movl %%eax,%%cr0\n jmp 1f\n1:\n"
                 " movw $8,%%bx\n movw %%bx,%%fs\n andb $0xFE,%%al\n movl %%eax,%%cr0\n jmp 2f\n2:\n"
                 " xorw %%bx,%%bx\n movw %%bx,%%fs\n popfl" : : "m"(gdtr) : "eax", "ebx", "memory", "cc");
}
static u32 prd32(u32 a) { u32 v; unreal_fs(); asm volatile("movl %%fs:(%1),%0" : "=r"(v) : "r"(a) : "memory"); return v; }
static void pwr32(u32 a, u32 v) { unreal_fs(); asm volatile("movl %0,%%fs:(%1)" :: "r"(v), "r"(a) : "memory"); }
static u32 ticks(void) { return prd32(0x46C); }
static u32 rd_cr0(void) { u32 v; asm volatile("movl %%cr0,%0" : "=r"(v)); return v; }
static void wr_cr0(u32 v) { asm volatile("movl %0,%%cr0" :: "r"(v) : "memory"); }

static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_open(const char *n, int create)
{ u32 ax = create ? 0x3C00 : 0x3D02; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static void dos_seek_end(int h) { u32 ax = 0x4202, cx = 0, dx = 0; asm volatile("int $0x21" : "+a"(ax), "+c"(cx), "+d"(dx) : "b"((u32)h) : "memory", "cc"); }

static char lbuf[512]; static int llen, lcreated;
static void lflush(void)
{
    int h;
    if (!llen) return;
    h = dos_open("CPUTEST.LOG", !lcreated);
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
          else { tmp[0] = c; sl = 1; }
          while (sl < width && n < max - 1) { o[n++] = zero ? '0' : ' '; width--; }
          for (i = 0; i < sl && n < max - 1; i++) o[n++] = s[i]; }
    }
    o[n] = 0; return n;
}
/* pr: ekran + log, zapis na dysk natychmiast (na wypadek zawieszenia) */
static void pr(const char *f, ...)
{
    char b[256]; va_list ap; int n, i;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    for (i = 0; i < n; i++) {
        if (b[i] == '\n') { dos_write(1, "\r\n", 2); if (llen < 500) { lbuf[llen++] = '\r'; lbuf[llen++] = '\n'; } }
        else { dos_write(1, &b[i], 1); if (llen < 510) lbuf[llen++] = b[i]; }
    }
    lflush();
}

/* ------------------------------------------------------------------ */
/* obsluga przerwan 6 (#UD) i 7 (#NM): licznik + przeskok instrukcji   */
/* ------------------------------------------------------------------ */
extern u16 isr_skip, isr7_count, isr6_count;
extern void isr7(void), isr6(void);
asm(
    ".code16\n"
    ".globl isr7, isr6, isr_skip, isr7_count, isr6_count\n"
    "isr_skip:   .word 0\n"
    "isr7_count: .word 0\n"
    "isr6_count: .word 0\n"
    "isr7:\n"
    "  pushw %bp\n  movw %sp,%bp\n  pushw %ax\n"
    "  movw %cs:isr_skip,%ax\n  addw %ax,2(%bp)\n"
    "  incw %cs:isr7_count\n"
    "  clts\n"
    "  popw %ax\n  popw %bp\n  iret\n"
    "isr6:\n"
    "  pushw %bp\n  movw %sp,%bp\n  pushw %ax\n"
    "  movw %cs:isr_skip,%ax\n  addw %ax,2(%bp)\n"
    "  incw %cs:isr6_count\n"
    "  popw %ax\n  popw %bp\n  iret\n"
    ".code16gcc\n");

static u32 old6, old7;
static void hook(void)
{
    u16 cs; asm volatile("movw %%cs,%0" : "=r"(cs));
    asm volatile("cli");
    old6 = prd32(6 * 4); old7 = prd32(7 * 4);
    pwr32(6 * 4, ((u32)cs << 16) | (u16)(u32)isr6);
    pwr32(7 * 4, ((u32)cs << 16) | (u16)(u32)isr7);
    asm volatile("sti");
}
static void unhook(void) { asm volatile("cli"); pwr32(6 * 4, old6); pwr32(7 * 4, old7); asm volatile("sti"); }

/* ------------------------------------------------------------------ */
/* test wydajnosci: 1 ms dzwieku jak w emulatorze SB                   */
/* ------------------------------------------------------------------ */
static u8 src8[4096];
static s16 dst[2 * 64];
static u32 bench_pos;
static void mix_1ms(u32 src_rate, int vol)
{
    /* 8-bit mono z czestotliwoscia src_rate -> 48 kHz stereo 16-bit, interpolacja liniowa */
    u32 step = (src_rate << 16) / 48000, k;
    for (k = 0; k < 48; k++) {
        u32 i = (bench_pos >> 16) & 4095, fr = bench_pos & 0xFFFF;
        s32 a = ((s32)src8[i] - 128) << 8, b = ((s32)src8[(i + 1) & 4095] - 128) << 8;
        s32 s = a + (((b - a) * (s32)(fr >> 1)) >> 15);
        s = (s * vol) >> 8;
        s += dst[2 * k];                   /* miksowanie z tym, co juz jest */
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        dst[2 * k] = (s16)s; dst[2 * k + 1] = (s16)s;
        bench_pos += step;
    }
}

int main(void)
{
    u16 cs; u32 cr0, t, n, i;
    asm volatile("movw %%cs,%0" : "=r"(cs));
    seg_base = (u32)cs << 4; gdtr.lim = 15; gdtr.base = seg_base + (u32)gdt;

    pr("CPUTEST 0.1 - diagnostyka procesora pod SBEMU (log: CPUTEST.LOG)\n");
    {   u16 msw; asm volatile("smsw %0" : "=r"(msw));
        if (msw & 1) { pr("Procesor w trybie V86 (EMM386/JEMM?). Testy FPU wymagaja trybu rzeczywistego.\n"
                          "Uruchom bez EMM386. Koniec.\n"); return 2; } }
    outb(0x92, (u8)((inb(0x92) | 2) & ~1));

    /* 1. CPU */
    cr0 = rd_cr0();
    pr("[1] CR0=%x  PE=%u MP=%u EM=%u TS=%u ET=%u NE=%u\n", cr0, cr0 & 1, (cr0 >> 1) & 1, (cr0 >> 2) & 1,
       (cr0 >> 3) & 1, (cr0 >> 4) & 1, (cr0 >> 5) & 1);
    {   u32 f1, f2; asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%0; pushl %0; popfl; pushfl; popl %0; pushl %1; popfl" : "=&r"(f1), "=&r"(f2));
        if ((f1 ^ f2) & 0x200000) { u32 a, b, c, d; char v[13];
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
            memcpy(v, &b, 4); memcpy(v + 4, &d, 4); memcpy(v + 8, &c, 4); v[12] = 0;
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
            pr("    CPUID: %s sygnatura %x flagi %x (FPU=%u)\n", v, a, d, d & 1);
        } else pr("    CPUID: brak\n"); }

    /* 2. pamiec */
    {   u32 ax; u16 top = *(volatile u16 *)2;   /* PSP:0002 = koniec pamieci */
        pr("[2] Pamiec konwencjonalna dostepna dla programow: %u KB\n", ((u32)(top - cs) * 16 + 256) / 1024);
        ax = 0x4300; asm volatile("int $0x2F" : "+a"(ax) :: "memory", "cc");
        pr("    XMS (HIMEM): %s\n", (ax & 0xFF) == 0x80 ? "jest" : "brak"); }

    hook();
    /* 3. pulapka EM=1 */
    pr("[3] Test pulapki: CR0.EM=1, wykonuje FLD1 (oczekiwane przerwanie 7)...\n");
    isr7_count = isr6_count = 0; isr_skip = 2;
    asm volatile("cli");
    wr_cr0((cr0 | 4) & ~8u);
    asm volatile(".byte 0xD9,0xE8" ::: "memory");          /* fld1 */
    wr_cr0(cr0);
    asm volatile("sti");
    pr("    przerwanie 7: %u razy, przerwanie 6: %u razy -> %s\n", isr7_count, isr6_count,
       isr7_count == 1 ? "OK, emulacja FPU przez pulapke jest mozliwa" :
       isr6_count ? "procesor zglasza nieznana instrukcje (#UD) zamiast #NM" : "BRAK pulapki - emulacja FPU niemozliwa w ten sposob");

    /* 3b. pulapka TS=1 (uzywana przez menedzery pamieci/DPMI do leniwego zapisu FPU) */
    pr("[3b] Test pulapki TS=1, EM=0, FNINIT...\n");
    isr7_count = isr6_count = 0; isr_skip = 2;
    asm volatile("cli");
    wr_cr0((cr0 & ~4u) | 8 | 2);                           /* TS=1, MP=1 */
    asm volatile(".byte 0xDB,0xE3" ::: "memory");          /* fninit */
    wr_cr0(cr0);
    asm volatile("sti");
    pr("    przerwanie 7: %u, przerwanie 6: %u\n", isr7_count, isr6_count);

    /* 4. klasyczne wykrywanie */
    pr("[4] Klasyczne wykrywanie FPU (FNINIT/FNSTSW przy EM=0)...\n");
    {   volatile u16 sw = 0x5A5A, cw = 0;
        isr7_count = isr6_count = 0; isr_skip = 2;
        wr_cr0(cr0 & ~0xCu);
        asm volatile(".byte 0xDB,0xE3" ::: "memory");                        /* fninit */
        asm volatile("fnstsw %0" : "=m"(sw));
        asm volatile("fnstcw %0" : "=m"(cw));
        wr_cr0(cr0);
        pr("    status=%x control=%x  przerw.7=%u 6=%u -> %s\n", sw, cw, isr7_count, isr6_count,
           ((sw & 0xFF) == 0 && (cw & 0x103F) == 0x003F) ? "FPU OBECNE" : "brak FPU");
    }

    /* 5. obliczenie przy EM=0 */
    pr("[5] FLD1+FLD1+FADDP+FISTP przy EM=0 (co robi procesor bez FPU)...\n");
    {   volatile s16 r = 0x1234;
        isr7_count = isr6_count = 0; isr_skip = 2;
        wr_cr0(cr0 & ~0xCu);
        asm volatile(".byte 0xD9,0xE8\n .byte 0xD9,0xE8\n .byte 0xDE,0xC1\n fistps %0" : "=m"(r) :: "memory");
        wr_cr0(cr0);
        pr("    wynik=%x (2 = dziala FPU, 1234 = instrukcje ignorowane)  przerw.7=%u 6=%u\n", (u32)(u16)r, isr7_count, isr6_count);
    }
    unhook();

    /* 6. wydajnosc */
    pr("[6] Wydajnosc miksowania (22050 Hz 8-bit -> 48 kHz stereo)...\n");
    for (i = 0; i < sizeof src8; i++) src8[i] = (u8)(128 + ((i * 37) & 63) - 32);
    t = ticks(); while (ticks() == t) ;
    t = ticks(); n = 0;
    while ((u32)(ticks() - t) < 18) { mix_1ms(22050, 200); n++; }
    {   u32 us = 989000 / (n ? n : 1);
        pr("    w ~1 s: %u ms dzwieku -> 1 ms dzwieku liczy sie ok. %u us (%u.%u%% CPU)\n", n, us, us / 10, us % 10); }
    {   /* petla pusta - surowa szybkosc */
        volatile u32 x = 0; t = ticks(); while (ticks() == t) ; t = ticks(); n = 0;
        while ((u32)(ticks() - t) < 9) { for (i = 0; i < 1000; i++) x += i; n++; }
        pr("    petla calkowita: %u tys. iteracji w 0.5 s\n", n);
    }
    pr("Koniec. Prosze przeslac CPUTEST.LOG\n");
    return 0;
}
