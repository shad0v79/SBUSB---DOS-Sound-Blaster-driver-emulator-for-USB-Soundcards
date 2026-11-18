/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * USBTONE.COM  -  test USB Audio (np. TI PCM2902) przez kontroler OHCI w DOS
 *
 * Etap 1 projektu "SB16 przez USB" dla eBOX-2300SX (Vortex86SX).
 * Program:
 *   1. skanuje PCI (OHCI / EHCI), zapisuje wszystko do USBTONE.LOG,
 *   2. ustala, do ktorego gniazda wlozono karte (wyjmij / wloz),
 *   3. przejmuje kontroler OHCI od BIOS-u (SMM),
 *   4. wylicza urzadzenie, parsuje deskryptory USB Audio Class 1.0,
 *   5. gra ton 1 kHz transferami izochronicznymi.
 *
 * Kompilacja: gcc -m16 (kod 16-bitowy tryb rzeczywisty), plik .COM.
 * Dostep do rejestrow MMIO powyzej 1 MB przez "unreal mode" (FS, limit 4 GB).
 * Wymaga: brak EMM386 (procesor nie moze byc w trybie V86).
 * Bez FPU - cala arytmetyka calkowitoliczbowa (Vortex86SX nie ma FPU).
 *
 * Licencja: MIT.
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed short   s16;
typedef signed int     s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg   __builtin_va_arg
#define va_end   __builtin_va_end

#include "sine.h"
#include "dbtab.h"

#define VERSION "0.3"
#define NULL ((void*)0)

/* ------------------------------------------------------------------ */
/* podstawy: pamiec, porty                                             */
/* ------------------------------------------------------------------ */
void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static inline void outb(u16 p, u8 v)  { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(u16 p, u32 v) { asm volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline u32  inl(u16 p)         { u32 v; asm volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }

static u32 seg_base;            /* CS*16 - adres liniowy poczatku segmentu */

/* ------------------------------------------------------------------ */
/* unreal mode: FS z limitem 4 GB                                      */
/* ------------------------------------------------------------------ */
static u8 gdt[16] __attribute__((aligned(8))) = {
    0,0,0,0,0,0,0,0,
    0xFF,0xFF,0x00,0x00,0x00,0x92,0xCF,0x00     /* dane, baza 0, limit 4G */
};
static struct { u16 lim; u32 base; } __attribute__((packed)) gdtr;

static void unreal_fs(void)
{
    asm volatile(
        "pushfl\n\t"
        "cli\n\t"
        "lgdtl %0\n\t"
        "movl %%cr0,%%eax\n\t"
        "orb  $1,%%al\n\t"
        "movl %%eax,%%cr0\n\t"
        "jmp 1f\n"
        "1:\n\t"
        "movw $8,%%bx\n\t"
        "movw %%bx,%%fs\n\t"
        "andb $0xFE,%%al\n\t"
        "movl %%eax,%%cr0\n\t"
        "jmp 2f\n"
        "2:\n\t"
        "xorw %%bx,%%bx\n\t"
        "movw %%bx,%%fs\n\t"
        "popfl\n\t"
        : : "m"(gdtr) : "eax", "ebx", "memory", "cc");
}

/* odczyt/zapis pod adresem fizycznym (MMIO lub RAM ponizej 1MB) */
static u32 prd32(u32 a) { u32 v; unreal_fs(); asm volatile("movl %%fs:(%1),%0" : "=r"(v) : "r"(a) : "memory"); return v; }
static void pwr32(u32 a, u32 v) { unreal_fs(); asm volatile("movl %0,%%fs:(%1)" :: "r"(v), "r"(a) : "memory"); }
static u8  prd8(u32 a)  { u8 v; unreal_fs(); asm volatile("movb %%fs:(%1),%0" : "=q"(v) : "r"(a) : "memory"); return v; }
static u16 prd16(u32 a) { u16 v; unreal_fs(); asm volatile("movw %%fs:(%1),%0" : "=r"(v) : "r"(a) : "memory"); return v; }

static int in_v86(void) { u16 msw; asm volatile("smsw %0" : "=r"(msw)); return msw & 1; }

/* ------------------------------------------------------------------ */
/* DOS / BIOS                                                          */
/* ------------------------------------------------------------------ */
static int dos_write(u16 h, const void *buf, u16 len)
{
    u32 ax = 0x4000; u8 cf;
    asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc");
    return cf ? -1 : (int)(ax & 0xFFFF);
}
static int dos_create(const char *n)
{
    u32 ax = 0x3C00; u8 cf;
    asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc");
    return cf ? -1 : (int)(ax & 0xFFFF);
}
static int dos_open(const char *n)
{
    u32 ax = 0x3D02; u8 cf;
    asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "d"(n) : "memory", "cc");
    return cf ? -1 : (int)(ax & 0xFFFF);
}
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static void dos_seek_end(int h) { u32 ax = 0x4202, cx = 0, dx = 0; asm volatile("int $0x21" : "+a"(ax), "+c"(cx), "+d"(dx) : "b"((u32)h) : "memory", "cc"); }

static u16 bios_getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }
static int bios_kbhit(void)
{
    u32 ax = 0x0100; u8 nz;
    asm volatile("int $0x16; setnz %1" : "+a"(ax), "=qm"(nz) :: "memory", "cc");
    return nz;
}
static u32 ticks(void) { return prd32(0x46C); }   /* 18.2 Hz */

static void wait_ticks(u32 n) { u32 t = ticks(); while ((u32)(ticks() - t) < n) ; }

/* ------------------------------------------------------------------ */
/* wyjscie na ekran + log                                              */
/* ------------------------------------------------------------------ */
#define LOGBUF 3584
static char logbuf[LOGBUF];
static int  loglen;
static int  log_ok = 1;          /* czy wolno pisac na dysk */
static const char logname[] = "USBTONE.LOG";
static int  log_created;
static int  verbose;

static void log_flush(void)
{
    int h;
    if (!loglen) return;
    if (!log_ok) return;
    if (!log_created) { h = dos_create(logname); log_created = 1; }
    else { h = dos_open(logname); if (h >= 0) dos_seek_end(h); }
    if (h >= 0) { dos_write((u16)h, logbuf, (u16)loglen); dos_close(h); }
    loglen = 0;
}

static void out_raw(const char *s, int n, int to_screen)
{
    int i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') {
            if (to_screen) dos_write(1, "\r\n", 2);
            if (loglen < LOGBUF - 2) { logbuf[loglen++] = '\r'; logbuf[loglen++] = '\n'; }
        } else {
            if (to_screen) dos_write(1, &c, 1);
            if (loglen < LOGBUF - 1) logbuf[loglen++] = c;
        }
    }
    if (loglen > LOGBUF - 256 && log_ok) log_flush();
}

static int fmt(char *o, int max, const char *f, va_list ap)
{
    int n = 0;
    static const char hx[] = "0123456789ABCDEF";
    while (*f) {
        char c = *f++;
        if (c != '%') { if (n < max - 1) o[n++] = c; continue; }
        {
            int zero = 0, width = 0, left = 0;
            char tmp[16]; int tl = 0; const char *s = tmp; int sl;
            if (*f == '-') { left = 1; f++; }
            if (*f == '0') { zero = 1; f++; }
            while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
            if (*f == 'l') f++;
            c = *f++;
            if (c == 'd' || c == 'u') {
                u32 v = va_arg(ap, u32); int neg = 0;
                if (c == 'd' && (s32)v < 0) { neg = 1; v = (u32)(-(s32)v); }
                do { tmp[tl++] = (char)('0' + v % 10); v /= 10; } while (v);
                if (neg) tmp[tl++] = '-';
                { int a = 0, b = tl - 1; while (a < b) { char t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t; a++; b--; } }
                sl = tl;
            } else if (c == 'x' || c == 'X') {
                u32 v = va_arg(ap, u32);
                do { tmp[tl++] = hx[v & 15]; v >>= 4; } while (v);
                { int a = 0, b = tl - 1; while (a < b) { char t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t; a++; b--; } }
                sl = tl;
            } else if (c == 's') {
                s = va_arg(ap, const char *); if (!s) s = "(null)"; sl = str_len(s);
            } else if (c == 'c') {
                tmp[0] = (char)va_arg(ap, int); sl = 1;
            } else { tmp[0] = c; sl = 1; }
            if (!left) while (sl < width && n < max - 1) { o[n++] = zero ? '0' : ' '; width--; }
            { int i; for (i = 0; i < sl && n < max - 1; i++) o[n++] = s[i]; }
            if (left) while (sl < width && n < max - 1) { o[n++] = ' '; width--; }
        }
    }
    o[n] = 0;
    return n;
}

/* pr: ekran + log;  lg: tylko log (lub ekran przy /V) */
static void pr(const char *f, ...)
{
    char b[256]; va_list ap; int n;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    out_raw(b, n, 1);
}
static void lg(const char *f, ...)
{
    char b[256]; va_list ap; int n;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    out_raw(b, n, verbose);
}

/* ------------------------------------------------------------------ */
/* PCI                                                                 */
/* ------------------------------------------------------------------ */
static u32 pci_rd(int b, int d, int f, int r)
{ outl(0xCF8, 0x80000000u | ((u32)b << 16) | ((u32)d << 11) | ((u32)f << 8) | (r & 0xFC)); return inl(0xCFC); }
static void pci_wr(int b, int d, int f, int r, u32 v)
{ outl(0xCF8, 0x80000000u | ((u32)b << 16) | ((u32)d << 11) | ((u32)f << 8) | (r & 0xFC)); outl(0xCFC, v); }

#define MAXHC 8
struct hc {
    int  ehci;          /* 0 = OHCI, 1 = EHCI */
    int  bus, dev, fn;
    u32  mmio;          /* baza rejestrow (operacyjnych dla EHCI) */
    u32  capbase;       /* EHCI: rejestry capability */
    int  nports;
    u32  vendev;
};
static struct hc hcs[MAXHC];
static int nhc;

static void pci_scan(void)
{
    int b, d, f;
    pr("Skanowanie PCI...\n");
    for (b = 0; b < 4; b++)
    for (d = 0; d < 32; d++)
    for (f = 0; f < 8; f++) {
        u32 id = pci_rd(b, d, f, 0), cls, hdr;
        if ((id & 0xFFFF) == 0xFFFF || id == 0) { if (f == 0) break; else continue; }
        cls = pci_rd(b, d, f, 8);
        hdr = pci_rd(b, d, f, 0x0C);
        lg("  PCI %d:%02x.%d  ID %04x:%04x  klasa %06x rev %02x\n", b, d, f,
           id & 0xFFFF, id >> 16, cls >> 8, cls & 0xFF);
        if ((cls >> 8) == 0x0C0310 || (cls >> 8) == 0x0C0320) {
            if (nhc < MAXHC) {
                struct hc *h = &hcs[nhc];
                u32 bar = pci_rd(b, d, f, 0x10), cmd;
                h->ehci = ((cls >> 8) == 0x0C0320);
                h->bus = b; h->dev = d; h->fn = f; h->vendev = id;
                if (bar & 1) { lg("    BAR0 to I/O (%x) - nieobslugiwane\n", bar); }
                else {
                    cmd = pci_rd(b, d, f, 4);
                    if ((cmd & 6) != 6) { pci_wr(b, d, f, 4, (cmd & 0xFFFF) | 6); lg("    wlaczono MEM+BusMaster\n"); }
                    if (h->ehci) {
                        h->capbase = bar & ~0xFu;
                        h->mmio = h->capbase + prd8(h->capbase);
                        h->nports = prd32(h->capbase + 4) & 0xF;
                    } else {
                        h->mmio = bar & ~0xFu;
                        h->nports = prd32(h->mmio + 0x48) & 0xFF;
                    }
                    pr("  #%d %s  PCI %d:%02x.%d  ID %04x:%04x  MMIO %08x  portow %d\n", nhc,
                       h->ehci ? "EHCI" : "OHCI", b, d, f, id & 0xFFFF, id >> 16, h->mmio, h->nports);
                    if (h->nports > 15) h->nports = 15;
                    nhc++;
                }
            }
        }
        if (f == 0 && !(hdr & 0x00800000)) break;   /* nie wielofunkcyjne */
    }
}

/* ------------------------------------------------------------------ */
/* OHCI - rejestry i struktury                                         */
/* ------------------------------------------------------------------ */
#define HcRevision        0x00
#define HcControl         0x04
#define HcCommandStatus   0x08
#define HcInterruptStatus 0x0C
#define HcInterruptEnable 0x10
#define HcInterruptDisable 0x14
#define HcHCCA            0x18
#define HcControlHeadED   0x20
#define HcControlCurrentED 0x24
#define HcBulkHeadED      0x28
#define HcBulkCurrentED   0x2C
#define HcFmInterval      0x34
#define HcFmNumber        0x3C
#define HcPeriodicStart   0x40
#define HcLSThreshold     0x44
#define HcRhDescriptorA   0x48
#define HcRhDescriptorB   0x4C
#define HcRhStatus        0x50
#define HcRhPortStatus    0x54
#define HceControl        0x100

#define CTL_CBSR   3
#define CTL_PLE    (1<<2)
#define CTL_IE     (1<<3)
#define CTL_CLE    (1<<4)
#define CTL_BLE    (1<<5)
#define CTL_HCFS   (3<<6)
#define CTL_OPER   (2<<6)
#define CTL_IR     (1<<8)
#define CTL_RWC    (1<<9)

#define PS_CCS  (1<<0)
#define PS_PES  (1<<1)
#define PS_PSS  (1<<2)
#define PS_PRS  (1<<4)
#define PS_PPS  (1<<8)
#define PS_LSDA (1<<9)
#define PS_CSC  (1<<16)
#define PS_PESC (1<<17)
#define PS_PRSC (1<<20)

static u32 ohci;                 /* baza MMIO wybranego OHCI */
static void log_all_ie(const char *when)
{
    int i;
    lg("IntEnable (%s):", when);
    for (i = 0; i < nhc; i++) if (!hcs[i].ehci) lg("  #%d=%08x", i, prd32(hcs[i].mmio + 0x10));
    lg("\n");
}
static u32 stop_k;               /* kopia opt_k */
static void park_hc(void);
#define KSTOP(n, txt) do { if (stop_k == (n)) { pr("/K:%u - zatrzymano po: %s\n", (u32)(n), txt); if ((n) >= 4) park_hc(); return 2; } } while (0)
static u32 orr(u32 r) { return prd32(ohci + r); }
static void owr(u32 r, u32 v) { pwr32(ohci + r, v); }
static u16 frame(void) { return (u16)orr(HcFmNumber); }

static void wait_frames(u16 n)
{
    u16 s = frame(); u32 t = ticks();
    while ((u16)(frame() - s) < n) {
        if ((u32)(ticks() - t) > (u32)(n / 50 + 3)) break;   /* licznik ramek stoi? */
    }
}

/* pamiec DMA (w naszym segmencie, ponizej 1 MB, adres fizyczny = liniowy) */
static u8 pool[0x6100];
static u32 dma_phys;              /* wyrownane do 4 KB */
#define PH(off)   (dma_phys + (off))
#define PTR(off)  ((volatile u32 *)(dma_phys + (off) - seg_base))
#define BPTR(off) ((volatile u8 *)(dma_phys + (off) - seg_base))

#define O_HCCA    0x0000
#define O_CED     0x0100        /* control ED */
#define O_IED     0x0110        /* iso ED */
#define O_CTD     0x0200        /* 4 TD sterujace po 32 B */
#define O_SETUP   0x0280
#define O_ITD     0x0400        /* NITD x 32 B */
#define O_CBUF    0x0800        /* 1024 B dane sterujace */
#define O_IBUF    0x1000        /* NITD x 256 B */
#define NITD      32
#define O_ITD2    0x3000        /* TD nagrywania (IN) */
#define O_IBUF2   0x4000        /* NITD x 256 B */
#define DMA_USED  0x6000

/* kody bledow OHCI */
static const char *cc_name(u32 cc)
{
    static const char *n[16] = { "OK", "CRC", "BitStuffing", "DataToggle", "Stall", "DevNotResponding",
        "PIDCheck", "UnexpectedPID", "DataOverrun", "DataUnderrun", "res10", "res11",
        "BufferOverrun", "BufferUnderrun", "NotAccessed", "NotAccessed" };
    return n[cc & 15];
}

/* ------------------------------------------------------------------ */
/* przejecie i inicjalizacja OHCI                                      */
/* ------------------------------------------------------------------ */
static int ohci_takeover(void)
{
    u32 ctl = orr(HcControl), rev = orr(HcRevision), fmi, i;
    lg("HcRevision=%08x HcControl=%08x FmInterval=%08x RhDescA=%08x RhDescB=%08x\n",
       rev, ctl, orr(HcFmInterval), orr(HcRhDescriptorA), orr(HcRhDescriptorB));
    if (rev & 0x100) lg("HceControl=%08x (emulacja legacy)\n", orr(HceControl));

    if (ctl & CTL_IR) {
        pr("Kontroler nalezy do BIOS (SMM) - prosba o oddanie...\n");
        owr(HcInterruptEnable, 1u << 30);           /* OwnershipChange */
        owr(HcCommandStatus, 1u << 3);              /* OCR */
        for (i = 0; i < 40 && (orr(HcControl) & CTL_IR); i++) wait_ticks(2);
        if (orr(HcControl) & CTL_IR) {
            pr("BLAD: BIOS nie oddal kontrolera (IR nadal 1).\n");
            pr("Sprobuj wylaczyc w BIOS 'USB Legacy Support' i uruchomic z CF.\n");
            return -1;
        }
        pr("BIOS oddal kontroler.\n");
    } else if ((ctl & CTL_HCFS) != 0) {
        lg("HCFS=%d (BIOS uzywal kontrolera bez SMM)\n", (ctl >> 6) & 3);
    }
    log_all_ie("przed");
    /* UWAGA (Vortex86SX): bit MIE jest najpewniej wspolny dla obu kontrolerow OHCI -
       jego skasowanie wylaczalo przerwania kontrolera z klawiatura USB. Kasujemy tylko
       zrodla przerwan, MIE (bit 31) zostaje. */
    owr(HcInterruptDisable, 0x4000007Fu);
    log_all_ie("po wylaczeniu zrodel");
    KSTOP(1, "wylaczeniu przerwan kontrolera");
    /* HceControl (emulacja klawiatury) celowo NIE jest ruszany - jego zapis
       wylaczal klawiature USB na drugim kontrolerze (eBOX-2300SX) */

    /* stan USBRESET na >= 50 ms */
    owr(HcControl, ctl & CTL_RWC);
    (void)orr(HcControl);
    wait_ticks(2);
    KSTOP(2, "przejsciu w stan USBRESET");

    fmi = orr(HcFmInterval) & 0x3FFF;
    if (fmi < 0x2E00 || fmi > 0x2FC0) { lg("FmInterval FI=%x nietypowy, uzywam 0x2EDF\n", fmi); fmi = 0x2EDF; }

    owr(HcCommandStatus, 1);                        /* HCR */
    for (i = 0; i < 100000 && (orr(HcCommandStatus) & 1); i++) ;
    if (orr(HcCommandStatus) & 1) { pr("BLAD: reset kontrolera (HCR) nie zakonczyl sie\n"); return -1; }
    log_all_ie("po HCR");
    owr(HcInterruptEnable, 0x80000000u);          /* przywroc MIE (patrz wyzej) */
    log_all_ie("po przywroceniu MIE");
    KSTOP(3, "resecie HCR (kontroler w stanie SUSPEND)");

    /* teraz USBSUSPEND - musimy wejsc w OPERATIONAL w ciagu 2 ms */
    memset((void *)PTR(0), 0, DMA_USED);
    owr(HcControlHeadED, 0);
    owr(HcBulkHeadED, 0);
    owr(HcHCCA, PH(O_HCCA));
    {
        u32 fsmps = (6 * (fmi - 210)) / 7;
        u32 fit = orr(HcFmInterval) & 0x80000000u;
        owr(HcFmInterval, (fit ^ 0x80000000u) | (fsmps << 16) | fmi);
        owr(HcPeriodicStart, (fmi * 9) / 10);
    }
    owr(HcControl, (ctl & CTL_RWC) | CTL_CBSR | CTL_OPER);
    owr(HcLSThreshold, 0x628);
    owr(HcInterruptStatus, 0x7F);

    lg("Po resecie: HcControl=%08x FmInterval=%08x PeriodicStart=%08x HCCA=%08x\n",
       orr(HcControl), orr(HcFmInterval), orr(HcPeriodicStart), orr(HcHCCA));
    if ((orr(HcFmInterval) & 0x3FFF0000u) == 0 || orr(HcPeriodicStart) == 0)
        pr("UWAGA: FmInterval/PeriodicStart nie przyjete przez kontroler\n");
    if (orr(HcHCCA) != PH(O_HCCA))
        pr("UWAGA: HcHCCA nie przyjal adresu %08x (odczyt %08x)\n", PH(O_HCCA), orr(HcHCCA));

    KSTOP(4, "ustawieniu HCCA/FmInterval i stanu OPERATIONAL");
    /* zasilanie portow (globalne) */
    {
        u32 a = orr(HcRhDescriptorA);
        u32 potpgt = (a >> 24) * 2;
        a &= ~((1u << 8) | (1u << 11));               /* PSM=0, OCPM=0 */
        owr(HcRhDescriptorA, a);
        owr(HcRhStatus, 1u << 16);                    /* LPSC = SetGlobalPower */
        owr(HcRhDescriptorB, (a & (1u << 9)) ? 0 : 0xFFFF0000u);
        if (potpgt < 100) potpgt = 100;
        wait_frames((u16)potpgt);
    }
    {
        u16 f1 = frame(); wait_ticks(2);
        lg("Licznik ramek: %u -> %u\n", f1, frame());
        if (f1 == frame()) { pr("BLAD: licznik ramek USB nie dziala (kontroler nie ruszyl)\n"); return -1; }
    }
    KSTOP(5, "konfiguracji root huba (zasilanie portow)");
    return 0;
}


/* Przed wyjsciem z programu: kontroler dalej pracuje (licznik ramek), ale jego HCCA
   wskazuje na trwale zarezerwowany blok pamieci (wlasciciel MCB = DOS), zeby nie
   zapisywal do pamieci zwolnionej po zakonczeniu programu. */
static void park_hc(void)
{
    u32 ax = 0x4800, bx = 48, seg, lin, m, i;
    if ((orr(HcControl) & CTL_HCFS) != CTL_OPER) return;
    owr(HcControl, orr(HcControl) & ~(CTL_PLE | CTL_IE | CTL_CLE | CTL_BLE));
    wait_frames(2);
    {   /* program .COM dostaje cala pamiec - najpierw zmniejsz wlasny blok do 64 KB */
        u32 a2 = 0x4A00, b2 = 0x1000;
        asm volatile("int $0x21" : "+a"(a2), "+b"(b2) :: "memory", "cc");   /* ES = PSP = CS */
    }
    {   u8 cf;
        asm volatile("int $0x21; setc %2" : "+a"(ax), "+b"(bx), "=qm"(cf) :: "memory", "cc");
        if (cf) { lg("Parkowanie: brak pamieci DOS (kod %u) - kontroler zatrzymany\n", ax & 0xFFFF);
                  owr(HcControl, orr(HcControl) & CTL_RWC); return; }
    }
    seg = ax & 0xFFFF;
    lin = ((seg << 4) + 255) & ~255u;
    m = prd32(((seg - 1) << 4)) ;                         /* MCB: bajt typu + slowo wlasciciela */
    pwr32(((seg - 1) << 4), (m & 0xFF0000FFu) | (0x0008u << 8));
    for (i = 0; i < 256; i += 4) pwr32(lin + i, 0);
    owr(HcControlHeadED, 0); owr(HcBulkHeadED, 0);
    owr(HcHCCA, lin);
    lg("Kontroler zaparkowany: HCCA %08x (blok DOS %04x na stale)\n", lin, seg);
}

/* ------------------------------------------------------------------ */
/* port root huba                                                      */
/* ------------------------------------------------------------------ */
static int port_reset(int p, int *ls)
{
    u32 r = HcRhPortStatus + 4 * p, st;
    int i, k;
    st = orr(r);
    lg("Port %d przed resetem: %08x\n", p + 1, st);
    if (!(st & PS_PPS)) { owr(r, PS_PPS); wait_frames(100); }
    if (!(orr(r) & PS_CCS)) { pr("Port %d: brak urzadzenia\n", p + 1); return -1; }
    wait_frames(100);                                 /* stabilizacja po podlaczeniu */
    for (k = 0; k < 5; k++) {                         /* 5 x 10 ms = 50 ms */
        owr(r, PS_PRS);
        for (i = 0; i < 50 && !(orr(r) & PS_PRSC); i++) wait_frames(1);
        owr(r, PS_PRSC);
    }
    owr(r, PS_CSC | PS_PESC);
    wait_frames(20);                                  /* reset recovery */
    st = orr(r);
    lg("Port %d po resecie: %08x\n", p + 1, st);
    if (!(st & PS_PES)) { pr("BLAD: port %d nie wlaczyl sie po resecie (%08x)\n", p + 1, st); return -1; }
    *ls = (st & PS_LSDA) ? 1 : 0;
    pr("Port %d wlaczony, predkosc: %s\n", p + 1, *ls ? "low (1.5M)" : "full (12M)");
    return 0;
}

/* ------------------------------------------------------------------ */
/* transfery sterujace                                                 */
/* ------------------------------------------------------------------ */
static int dev_ls;

static int control(u8 addr, u16 mps, u8 rt, u8 req, u16 val, u16 idx, u16 len, void *data, int *actual)
{
    volatile u32 *ed = PTR(O_CED);
    volatile u32 *td0 = PTR(O_CTD), *td1 = PTR(O_CTD + 32), *td2 = PTR(O_CTD + 64), *tdd = PTR(O_CTD + 96);
    volatile u8 *su = BPTR(O_SETUP);
    int in = (rt & 0x80) != 0, i;
    u32 last, cc;
    u16 t0;

    if (len > 1024) len = 1024;
    /* zatrzymaj liste sterujaca na czas modyfikacji */
    owr(HcControl, orr(HcControl) & ~CTL_CLE);
    wait_frames(2);

    su[0] = rt; su[1] = req; su[2] = (u8)val; su[3] = (u8)(val >> 8);
    su[4] = (u8)idx; su[5] = (u8)(idx >> 8); su[6] = (u8)len; su[7] = (u8)(len >> 8);
    if (!in && len) memcpy((void *)BPTR(O_CBUF), data, len);

    for (i = 0; i < 4 * 8; i++) td0[i] = 0;
    /* SETUP, DATA0 */
    td0[0] = (0xFu << 28) | (2u << 24) | (7u << 21) | (0u << 19);
    td0[1] = PH(O_SETUP); td0[3] = PH(O_SETUP) + 7;
    last = PH(O_CTD);
    if (len) {
        td0[2] = PH(O_CTD + 32);
        td1[0] = (0xFu << 28) | (3u << 24) | (7u << 21) | ((in ? 2u : 1u) << 19) | (in ? (1u << 18) : 0);
        td1[1] = PH(O_CBUF); td1[3] = PH(O_CBUF) + len - 1;
        td1[2] = PH(O_CTD + 64);
    } else td0[2] = PH(O_CTD + 64);
    /* STATUS, DATA1, kierunek przeciwny */
    td2[0] = (0xFu << 28) | (3u << 24) | (7u << 21) | ((in && len ? 1u : 2u) << 19);
    td2[1] = 0; td2[3] = 0; td2[2] = PH(O_CTD + 96);
    (void)last; (void)tdd;

    ed[0] = addr | (0u << 7) | (dev_ls ? (1u << 13) : 0) | ((u32)mps << 16);
    ed[1] = PH(O_CTD + 96);       /* TailP = dummy */
    ed[2] = PH(O_CTD);            /* HeadP */
    ed[3] = 0;
    owr(HcControlHeadED, PH(O_CED));
    owr(HcControlCurrentED, 0);
    owr(HcControl, orr(HcControl) | CTL_CLE);
    owr(HcCommandStatus, 1u << 1);                    /* CLF */

    t0 = frame();
    while (((ed[2] & ~0xFu) != PH(O_CTD + 96)) && !(ed[2] & 1)) {
        if ((u16)(frame() - t0) > 1000) break;
    }
    if (actual) *actual = 0;
    {
        u32 c0 = td0[0] >> 28, c1 = len ? (td1[0] >> 28) : 0, c2 = td2[0] >> 28;
        cc = c0 ? c0 : (c1 ? c1 : c2);
        if ((ed[2] & ~0xFu) != PH(O_CTD + 96) && !(ed[2] & 1) && cc == 0xF) cc = 0x10;  /* timeout */
        if (cc) {
            lg("  CTRL rt=%02x req=%02x val=%04x idx=%04x len=%u: blad %s (setup=%x data=%x status=%x HeadP=%08x)\n",
               rt, req, val, idx, len, cc == 0x10 ? "TIMEOUT" : cc_name(cc), c0, c1, c2, ed[2]);
            ed[2] = PH(O_CTD + 96);   /* zdejmij halt */
            return cc == 0x10 ? -2 : -1;
        }
        if (len && in) {
            u32 n = td1[1] ? (td1[1] - PH(O_CBUF)) : len;
            if (n > len) n = len;
            memcpy(data, (void *)BPTR(O_CBUF), n);
            if (actual) *actual = (int)n;
        } else if (actual) *actual = len;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* skan portow (wyjmij / wloz)                                         */
/* ------------------------------------------------------------------ */
static u32 port_map[MAXHC];

static void scan_ports(u32 *map, int show)
{
    int i, p;
    for (i = 0; i < nhc; i++) {
        map[i] = 0;
        for (p = 0; p < hcs[i].nports; p++) {
            u32 st;
            if (hcs[i].ehci) {
                st = prd32(hcs[i].mmio + 0x44 + 4 * p);
                if ((st & 1) && !(st & (1u << 13))) map[i] |= 1u << p;
            } else {
                st = prd32(hcs[i].mmio + HcRhPortStatus + 4 * p);
                if (st & 1) map[i] |= 1u << p;
            }
            if (show) lg("  #%d %s port %d: %08x%s\n", i, hcs[i].ehci ? "EHCI" : "OHCI", p + 1, st,
                         (map[i] >> p) & 1 ? "  <- urzadzenie" : "");
        }
    }
}

static void wait_enter(void)
{
    for (;;) {
        u16 k = bios_getkey();
        if ((k & 0xFF) == 13) return;
        if ((k & 0xFF) == 27) { pr("Przerwano.\n"); log_flush(); asm volatile("movw $0x4C01,%%ax; int $0x21" ::: "eax"); }
    }
}

/* EHCI: oddaj port (pelna/niska predkosc) do kontrolera towarzyszacego OHCI */
static void ehci_release(int i, int p)
{
    u32 r = hcs[i].mmio + 0x44 + 4 * p, st = prd32(r);
    lg("EHCI #%d port %d: PORTSC=%08x CONFIGFLAG=%08x\n", i, p + 1, st, prd32(hcs[i].mmio + 0x40));
    if (st & 4) { pr("Port EHCI %d jest aktywny (urzadzenie high-speed?) - nie ruszam\n", p + 1); return; }
    pwr32(r, (st & ~0x2Au) | (1u << 13));     /* PortOwner=1, bez kasowania bitow RWC */
    pr("Przekazano port EHCI %d do kontrolera OHCI.\n", p + 1);
    wait_ticks(10);
}

/* ------------------------------------------------------------------ */
/* USB Audio Class 1.0                                                 */
/* ------------------------------------------------------------------ */
struct alt {
    u8 ifn, alt, ep, nch, sub, bits, nfreq, fctl, sync;
    u16 mps;
    u32 fmin, fmax;
    u32 freq[8];
};
#define MAXALT 8
static struct alt alts[MAXALT];
static int nalts;
static struct alt alts_in[MAXALT];
static int nalts_in;
static u8 ac_if = 0xFF, fu_id[4]; static int nfu;

static void parse_config(const u8 *c, int len)
{
    int i = 0, cls = 0, sub = 0, ifn = 0, altn = 0;
    struct alt cur; int have_fmt = 0;
    memset(&cur, 0, sizeof cur);
    while (i + 2 <= len) {
        int bl = c[i], bt = c[i + 1];
        const u8 *d = c + i;
        if (bl < 2 || i + bl > len) break;
        if (bt == 4 && bl >= 9) {
            ifn = d[2]; altn = d[3]; cls = d[5]; sub = d[6]; have_fmt = 0;
            lg("  Interfejs %d alt %d klasa %02x/%02x endpointow %d\n", ifn, altn, cls, sub, d[4]);
            if (cls == 1 && sub == 1) ac_if = (u8)ifn;
        } else if (bt == 0x24 && cls == 1 && sub == 1) {
            if (d[2] == 6 && nfu < 4) { fu_id[nfu++] = d[3]; lg("    Feature Unit ID %d\n", d[3]); }
            else lg("    AC deskryptor podtyp %d ID %d\n", d[2], bl > 3 ? d[3] : 0);
        } else if (bt == 0x24 && cls == 1 && sub == 2) {
            if (d[2] == 2 && bl >= 8 && d[3] == 1) {
                int k;
                memset(&cur, 0, sizeof cur);
                cur.ifn = (u8)ifn; cur.alt = (u8)altn;
                cur.nch = d[4]; cur.sub = d[5]; cur.bits = d[6]; cur.nfreq = d[7];
                if (cur.nfreq == 0 && bl >= 14) {
                    cur.fmin = d[8] | (d[9] << 8) | ((u32)d[10] << 16);
                    cur.fmax = d[11] | (d[12] << 8) | ((u32)d[13] << 16);
                    lg("    Format: %d kan, %d bit, ciagle %u-%u Hz\n", cur.nch, cur.bits, cur.fmin, cur.fmax);
                } else {
                    for (k = 0; k < cur.nfreq && k < 8 && 8 + 3 * k + 2 < bl; k++)
                        cur.freq[k] = d[8 + 3 * k] | (d[9 + 3 * k] << 8) | ((u32)d[10 + 3 * k] << 16);
                    if (cur.nfreq > 8) cur.nfreq = 8;
                    lg("    Format: %d kan, %d bit (%d B), czestotliwosci:", cur.nch, cur.bits, cur.sub);
                    for (k = 0; k < cur.nfreq; k++) lg(" %u", cur.freq[k]);
                    lg("\n");
                }
                have_fmt = 1;
            } else lg("    AS deskryptor podtyp %d\n", d[2]);
        } else if (bt == 5 && bl >= 7) {
            lg("    Endpoint %02x atrybuty %02x MPS %u interwal %d\n", d[2], d[3], d[4] | (d[5] << 8), d[6]);
            if (cls == 1 && sub == 2 && have_fmt && (d[3] & 3) == 1 && !(d[2] & 0x80) && nalts < MAXALT) {
                cur.ep = d[2] & 0x0F; cur.mps = (u16)((d[4] | (d[5] << 8)) & 0x7FF);
                cur.sync = (d[3] >> 2) & 3;
                alts[nalts++] = cur;
            }
            if (cls == 1 && sub == 2 && have_fmt && (d[3] & 3) == 1 && (d[2] & 0x80) && nalts_in < MAXALT) {
                cur.ep = d[2] & 0x0F; cur.mps = (u16)((d[4] | (d[5] << 8)) & 0x7FF);
                cur.sync = (d[3] >> 2) & 3;
                alts_in[nalts_in++] = cur;
            }
        } else if (bt == 0x25 && bl >= 4) {
            if (nalts && have_fmt) alts[nalts - 1].fctl = d[3] & 1;
            lg("    CS endpoint atrybuty %02x\n", d[3]);
        }
        i += bl;
    }
}

static int alt_has_rate(struct alt *a, u32 r)
{
    int k;
    if (a->nfreq == 0) return r >= a->fmin && r <= a->fmax;
    for (k = 0; k < a->nfreq; k++) if (a->freq[k] == r) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* glowna petla izochroniczna                                          */
/* ------------------------------------------------------------------ */
static u32 rate = 48000, tone_hz = 1000, seconds = 10;
static int lr_test;

static int stream(u8 addr, struct alt *a)
{
    volatile u32 *ed = PTR(O_IED);
    volatile u32 *hcca = PTR(O_HCCA);
    int h = 0, t = 0, i;
    u16 next;
    u32 phase = 0, inc, frac = 0, base_spf = rate / 1000, rem_spf = rate % 1000;
    u32 sent = 0, errs = 0, late = 0, underruns = 0, cccount[16];
    u32 t_start, t_last;
    int ch = a->nch;
    u32 frames_total = seconds * 1000, queued_total = 0;
    int curside = 0;

    memset(cccount, 0, sizeof cccount);
    {   /* inc = tone * 2^32 / rate bez 64-bit */
        u32 x = tone_hz << 16;
        inc = ((x / rate) << 16) + (((x % rate) << 16) / rate);
    }
    for (i = 0; i < NITD * 8; i++) PTR(O_ITD)[i] = 0;

    ed[0] = addr | ((u32)a->ep << 7) | (1u << 11) | (dev_ls ? (1u << 13) : 0) | (1u << 15) | ((u32)a->mps << 16);
    ed[1] = PH(O_ITD);   /* TailP */
    ed[2] = PH(O_ITD);   /* HeadP: pusto */
    ed[3] = 0;
    for (i = 0; i < 32; i++) hcca[i] = PH(O_IED);
    owr(HcControl, orr(HcControl) | CTL_PLE | CTL_IE);

    pr("Gram %u Hz przez %u s (%u Hz, %d kan.)  - ESC przerywa\n", tone_hz, seconds, rate, ch);
    next = (u16)(frame() + 8);
    t_start = t_last = ticks();

    while (sent + 0 < frames_total || h != t) {
        /* 1. zbierz zakonczone TD */
        u32 hp = ed[2] & ~0xFu;
        if (ed[2] & 1) {
            lg("ED zatrzymany (halt), HeadP=%08x - wznawiam\n", ed[2]);
            ed[2] = hp; errs++;
        }
        while (h != t && PH(O_ITD + 32 * h) != hp) {
            volatile u32 *td = PTR(O_ITD + 32 * h);
            u32 cc = td[0] >> 28, pcc = (td[4] >> 12) & 0xF;
            if (cc == 8) late++;
            else if (cc || pcc) { errs++; cccount[cc ? cc : pcc]++; }
            sent++;
            h = (h + 1) % NITD;
        }
        /* 2. dosypuj */
        while (((t + 1) % NITD) != h && queued_total < frames_total) {
            volatile u32 *td = PTR(O_ITD + 32 * t);
            volatile u32 *nd = PTR(O_ITD + 32 * ((t + 1) % NITD));
            volatile s16 *buf = (volatile s16 *)BPTR(O_IBUF + 256 * t);
            u32 n = base_spf, bytes, k;
            u16 now = frame();
            if ((s16)(next - now) < 2) { next = (u16)(now + 4); underruns++; }
            frac += rem_spf; if (frac >= 1000) { frac -= 1000; n++; }
            if (lr_test) curside = (queued_total / 1000) & 1;
            for (k = 0; k < n; k++) {
                s16 s = sine256[phase >> 24];
                phase += inc;
                if (ch == 1) buf[k] = s;
                else {
                    buf[2 * k]     = (lr_test && curside)  ? 0 : s;
                    buf[2 * k + 1] = (lr_test && !curside) ? 0 : s;
                    for (i = 2; i < ch; i++) buf[ch * k + i] = 0;
                }
            }
            bytes = n * ch * 2;
            nd[0] = nd[1] = nd[2] = nd[3] = nd[4] = nd[5] = nd[6] = nd[7] = 0;
            td[1] = PH(O_IBUF + 256 * t) & ~0xFFFu;
            td[2] = PH(O_ITD + 32 * ((t + 1) % NITD));
            td[3] = PH(O_IBUF + 256 * t) + bytes - 1;
            td[4] = 0xE000u | (PH(O_IBUF + 256 * t) & 0xFFFu);
            td[5] = td[6] = td[7] = 0;
            td[0] = (0xFu << 28) | (0u << 24) | (7u << 21) | next;
            next++;
            t = (t + 1) % NITD;
            ed[1] = PH(O_ITD + 32 * t);                 /* nowy TailP */
            queued_total++;
        }
        /* 3. postep / klawiatura */
        if ((u32)(ticks() - t_last) >= (lr_test ? 3u : 18u)) {
            t_last = ticks();
            pr("\r  ramek: %u  bledow: %u  spoznionych: %u  luk: %u%s", sent, errs, late, underruns,
               lr_test ? ((sent / 1000) & 1 ? "   teraz gra: PRAWY kanal " : "   teraz gra: LEWY kanal  ") : "   ");
            /* skasuj kolejke "done" */
            owr(HcInterruptStatus, 2);
        }
        if (bios_kbhit()) { u16 k = bios_getkey(); if ((k & 0xFF) == 27) { frames_total = queued_total; } }
        if ((u32)(ticks() - t_start) > (seconds + 5) * 19) { pr("\nTimeout petli (ramki nie sa przetwarzane?)\n"); break; }
    }
    pr("\n");
    owr(HcControl, orr(HcControl) & ~(CTL_PLE | CTL_IE));
    pr("Wyslano ramek: %u, bledow: %u, spoznionych: %u, luk w kolejce: %u\n", sent, errs, late, underruns);
    for (i = 0; i < 16; i++) if (cccount[i]) pr("  kod %s: %u\n", cc_name(i), cccount[i]);
    return errs ? 1 : 0;
}


/* ------------------------------------------------------------------ */
/* XMS (pamiec na nagranie)                                            */
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

static int db_of(u32 v)   /* -dBFS (0..100) */
{
    int d = 0;
    while (d < 100 && dbtab[d] > v) d++;
    return d;
}
static u32 isqrt(u32 x) { u32 r = 0, b = 1u << 30; while (b > x) b >>= 2; while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

/* ------------------------------------------------------------------ */
/* pomiar w petli LINE OUT -> LINE IN                                  */
/* ------------------------------------------------------------------ */
#define REC_MS    4000
#define O_IED2    0x0120
static u32 xaddr;
static int loop_sim;
static u32 loop_db = 30;   /* poziom tonu w /LOOP: -30 dBFS */   /* test samej analizy: syntetyczne nagranie (opcja /LOOPSIM) */
static s16 recs(u32 i, int ch) { u32 v = prd32(xaddr + i * 4); return (s16)(ch ? (v >> 16) : (v & 0xFFFF)); }

static void analyse(u32 n, int ch, u32 hz, u32 c15, u32 onset, int *bad)
{
    u32 i, peak = 0, clip = 0, w0, w1, cross = 0, ev = 0, last = 0, thr, shown = 0;
    for (i = 0; i < n; i++) { s32 x = recs(i, ch); u32 a = x < 0 ? -x : x; if (a > peak) peak = a; if (a >= 32700) clip++; }
    pr("  Kanal %s (ton %u Hz): szczyt %u (-%u dBFS)%s\n", ch ? "PRAWY" : "LEWY ", hz, peak, db_of(peak), clip ? "  PRZESTEROWANIE!" : "");
    if (clip) (*bad)++;
    if (peak < 1000) { pr("    za slaby sygnal - sprawdz kabel (LINE OUT -> LINE IN)\n"); (*bad)++; return; }
    w0 = onset + 9600; w1 = w0 + 120000; if (w1 > n - 2) w1 = n - 2;
    if (w1 <= w0 + 4800) { pr("    za krotkie nagranie do analizy\n"); (*bad)++; return; }
    for (i = w0 + 1; i < w1; i++) if (recs(i - 1, ch) < 0 && recs(i, ch) >= 0) cross++;
    {   u32 f10 = cross * 480000 / (w1 - w0 - 1);
        pr("    zmierzona czestotliwosc: %u.%u Hz (oczekiwane %u)\n", f10 / 10, f10 % 10, hz);
        if (f10 < hz * 10 - hz / 10 || f10 > hz * 10 + hz / 10) (*bad)++; }
    thr = peak / 8; if (thr < 400) thr = 400;
    for (i = w0 + 1; i < w1 - 1; i++) {
        s32 a = recs(i - 1, ch), b = recs(i, ch), c = recs(i + 1, ch);
        s32 r;
        if (a >= 32000 || a <= -32000 || b >= 32000 || b <= -32000 || c >= 32000 || c <= -32000) continue;
        r = a + c - (s32)(((s32)c15 * b) >> 14);
        if (r < 0) r = -r;
        if ((u32)r > thr && (ev == 0 || i > last + 48)) {
            ev++;
            if (shown < 5) { lg("    zaklocenie przy %u ms (odchylka %u)\n", i / 48, r); shown++; }
            last = i;
        }
    }
    pr("    zaklocen ksztaltu fali w %u s: %u%s\n", (w1 - w0) / 48000, ev, clip ? " (przesterowane probki pominiete)" : "");
    if (ev) (*bad)++;
    {   /* regularnosc okresow: przejscia przez zero z interpolacja (1/256 probki) */
        u32 prev = 0, have = 0, irr = 0, per = 0, exp256 = (48000u * 256) / hz, shown2 = 0;
        for (i = w0 + 1; i < w1; i++) {
            s32 a = recs(i - 1, ch), b = recs(i, ch);
            if (a < 0 && b >= 0) {
                u32 t = i * 256 - (u32)(((s32)256 * b) / (b - a));
                if (have) {
                    u32 d = t - prev, dev = d > exp256 ? d - exp256 : exp256 - d;
                    per++;
                    if (dev > 128) { irr++; if (shown2 < 5) { lg("    nieregularny okres przy %u ms (%u/256 probki)\n", i / 48, d); shown2++; } }
                }
                prev = t; have = 1;
            }
        }
        pr("    okresow: %u, nieregularnych (zgubione/podwojone probki): %u\n", per, irr);
        if (irr) (*bad)++;
    }
}

static int loop_test(u8 addr, struct alt *ao, struct alt *ai)
{
    volatile u32 *edo = PTR(O_IED), *edi = PTR(O_IED2), *hcca = PTR(O_HCCA);
    u32 xh, xkb = 1024, rec_len = 0, rec_max = 1024 * 1024 - 1024;
    int ho = 0, to = 0, hi = 0, ti = 0, i, bad = 0;
    u32 fo = 0, fi = 0, done_o = 0, done_i = 0, total = REC_MS, err_o = 0, err_i = 0, late = 0, empty = 0;
    u32 pL = 0, pR = 0, iL = ((1000u << 16) / 48000) << 16, iR = ((1500u << 16) / 48000) << 16;
    u16 nf;
    u32 t_start;

    if (!xms_init()) { pr("BLAD: brak XMS (HIMEM/HIMEMX) - potrzebny do nagrania\n"); return 1; }
    {   u32 h = xkb, dx, bx = 0;
        if (xms_call(0x0900, &h, NULL) != 1) { pr("BLAD: XMS nie przydzielil %u KB\n", xkb); return 1; }
        xh = h; dx = xh;
        if (xms_call(0x0C00, &dx, &bx) != 1) { pr("BLAD: nie mozna zablokowac bloku XMS\n"); h = xh; xms_call(0x0A00, &h, NULL); return 1; }
        xaddr = (dx << 16) | bx;
    }
    lg("XMS: uchwyt %x adres %08x\n", xh, xaddr);
    if (xaddr < 0x100000) { u32 h = xh; pr("BLAD: dziwny adres XMS %08x\n", xaddr); xms_call(0x0D00, &h, NULL); h = xh; xms_call(0x0A00, &h, NULL); return 1; }

    if (loop_sim) {   /* 20 ms opoznienia, ton L 1000 / P 1500, jeden trzask przy 2000 ms */
        u32 k, a = 0, b = 0;
        for (k = 0; k < 4000u * 48; k++) {
            s16 l = 0, r = 0;
            if (k >= 520u * 48 && k < 3520u * 48) { l = (s16)(sine256[a >> 24] / 2); r = (s16)(sine256[b >> 24] / 2); a += iL; b += iR; }
            if (k == 2000u * 48) { l = 20000; }
            pwr32(xaddr + k * 4, ((u32)(u16)r << 16) | (u16)l);
        }
        rec_len = 4000u * 48 * 4; done_o = done_i = total;
        goto analysis;
    }
    for (i = 0; i < NITD * 8; i++) { PTR(O_ITD)[i] = 0; PTR(O_ITD2)[i] = 0; }
    edo[0] = addr | ((u32)ao->ep << 7) | (1u << 11) | (dev_ls ? (1u << 13) : 0) | (1u << 15) | ((u32)ao->mps << 16);
    edo[1] = edo[2] = PH(O_ITD); edo[3] = PH(O_IED2);
    edi[0] = addr | ((u32)ai->ep << 7) | (2u << 11) | (dev_ls ? (1u << 13) : 0) | (1u << 15) | ((u32)ai->mps << 16);
    edi[1] = edi[2] = PH(O_ITD2); edi[3] = 0;
    for (i = 0; i < 32; i++) hcca[i] = PH(O_IED);

    pr("Pomiar: 0.5 s ciszy, 3 s tonu (L 1000 Hz, P 1500 Hz, -%u dBFS), 0.5 s ciszy.\n", loop_db);
    owr(HcControl, orr(HcControl) | CTL_PLE | CTL_IE);
    nf = (u16)(frame() + 10);
    {   u16 nfo = nf, nfi = nf;
        t_start = ticks();
        while (done_o < total || done_i < total) {
            u32 hp;
            /* zakonczone OUT */
            hp = edo[2]; if (hp & 1) { edo[2] = hp & ~1u; err_o++; } hp &= ~0xFu;
            while (ho != to && PH(O_ITD + 32 * ho) != hp) {
                u32 cc = PTR(O_ITD + 32 * ho)[0] >> 28;
                if (cc == 8) late++; else if (cc) err_o++;
                done_o++; ho = (ho + 1) % NITD;
            }
            /* zakonczone IN */
            hp = edi[2]; if (hp & 1) { edi[2] = hp & ~1u; err_i++; } hp &= ~0xFu;
            while (hi != ti && PH(O_ITD2 + 32 * hi) != hp) {
                volatile u32 *td = PTR(O_ITD2 + 32 * hi);
                u32 cc = td[0] >> 28, psw = td[4] & 0xFFFF, pcc = psw >> 12, sz = psw & 0x7FF, k;
                if (cc == 8) late++;
                else if ((pcc == 0 || pcc == 9) && sz) {
                    if (rec_len + sz <= rec_max)
                        for (k = 0; k + 3 < sz; k += 4)
                            pwr32(xaddr + rec_len + k, *(volatile u32 *)BPTR(O_IBUF2 + 256 * hi + k));
                    rec_len += sz & ~3u;
                } else if (pcc == 0 || pcc == 9) empty++;
                else err_i++;
                done_i++; hi = (hi + 1) % NITD;
            }
            /* dosypuj OUT */
            while (((to + 1) % NITD) != ho && fo < total) {
                volatile u32 *td = PTR(O_ITD + 32 * to);
                volatile u32 *nd = PTR(O_ITD + 32 * ((to + 1) % NITD));
                volatile s16 *b = (volatile s16 *)BPTR(O_IBUF + 256 * to);
                u32 k;
                for (k = 0; k < 48; k++) {
                    s16 l = 0, r = 0;
                    if (fo >= 500 && fo < 3500) {
                        l = (s16)(((s32)sine256[pL >> 24] * (s32)dbtab[loop_db]) / 12000);
                        r = (s16)(((s32)sine256[pR >> 24] * (s32)dbtab[loop_db]) / 12000);
                    }
                    pL += iL; pR += iR;
                    b[2 * k] = l; b[2 * k + 1] = r;
                }
                nd[0] = nd[1] = nd[2] = nd[3] = nd[4] = nd[5] = nd[6] = nd[7] = 0;
                td[1] = PH(O_IBUF + 256 * to) & ~0xFFFu;
                td[2] = PH(O_ITD + 32 * ((to + 1) % NITD));
                td[3] = PH(O_IBUF + 256 * to) + 192 - 1;
                td[4] = 0xE000u | (PH(O_IBUF + 256 * to) & 0xFFFu);
                td[5] = td[6] = td[7] = 0;
                td[0] = (0xFu << 28) | (7u << 21) | nfo;
                nfo++; fo++;
                to = (to + 1) % NITD;
                edo[1] = PH(O_ITD + 32 * to);
            }
            /* dosypuj IN */
            while (((ti + 1) % NITD) != hi && fi < total) {
                volatile u32 *td = PTR(O_ITD2 + 32 * ti);
                volatile u32 *nd = PTR(O_ITD2 + 32 * ((ti + 1) % NITD));
                u32 bp = PH(O_IBUF2 + 256 * ti);
                nd[0] = nd[1] = nd[2] = nd[3] = nd[4] = nd[5] = nd[6] = nd[7] = 0;
                td[1] = bp & ~0xFFFu;
                td[2] = PH(O_ITD2 + 32 * ((ti + 1) % NITD));
                td[3] = bp + ai->mps - 1;
                td[4] = 0xE000u | (bp & 0xFFFu);
                td[5] = td[6] = td[7] = 0;
                td[0] = (0xFu << 28) | (7u << 21) | nfi;
                nfi++; fi++;
                ti = (ti + 1) % NITD;
                edi[1] = PH(O_ITD2 + 32 * ti);
            }
            if ((u32)(ticks() - t_start) > (REC_MS / 55) + 60) { pr("Timeout petli pomiarowej\n"); break; }
        }
    }
    owr(HcControl, orr(HcControl) & ~(CTL_PLE | CTL_IE));
analysis:
    pr("Transmisja: OUT %u ramek (bledow %u), IN %u ramek (bledow %u, pustych %u), spoznionych %u\n",
       done_o, err_o, done_i, err_i, empty, late);
    if (err_o || err_i || late) bad++;
    {   u32 n = rec_len / 4, onset = 0, k;
        pr("Nagrano %u probek stereo (%u ms)\n", n, n / 48);
        {   u32 pk = 0, th;
            for (k = 0; k < n; k++) { s32 x = recs(k, 0); u32 a = x < 0 ? -x : x; if (a > pk) pk = a; }
            th = pk / 4; if (th < 300) th = 300;
            if (pk >= 1000) for (k = 0; k < n; k++) { s32 x = recs(k, 0); if (x > (s32)th || x < -(s32)th) { onset = k; break; } } }
        if (!onset) { pr("Nie wykryto tonu na wejsciu! Sprawdz polaczenie LINE OUT -> LINE IN.\n"); bad++; }
        else {
            s32 lat10 = (s32)(onset * 10 / 48) - 5000;
            pr("Opoznienie wyjscie->wejscie: ok. %d.%u ms\n", lat10 / 10, (u32)(lat10 < 0 ? -lat10 : lat10) % 10);
            analyse(n, 0, 1000, 32488, onset, &bad);
            analyse(n, 1, 1500, 32138, onset, &bad);
            {   /* szum w koncowej ciszy */
                u32 a = (3500 * 48) + (onset > 24000 ? onset - 24000 : 0) + 4800, cnt = 0, sum = 0, rms;
                for (k = a; k + 480 < n; k++) { s32 x = recs(k, 0); u32 q = (u32)(x * x); if (q > 1000000) q = 1000000; sum += q >> 4; cnt++; }
                if (cnt > 4800) { rms = isqrt(sum / cnt * 16); pr("  Szum (lewy, cisza): RMS %u (-%u dBFS)\n", rms, db_of(rms ? rms : 1)); }
            }
        }
        /* LOOP.WAV */
        if (log_ok && n) {
            int h = dos_create("LOOP.WAV");
            if (h >= 0) {
                u8 *wb = (u8 *)BPTR(O_IBUF); u32 dl = n * 4, done = 0;
                static const u8 hdr0[] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                                           0x80,0xBB,0,0, 0x00,0xEE,0x02,0, 4,0,16,0, 'd','a','t','a',0,0,0,0 };
                memcpy(wb, hdr0, 44);
                { u32 r = dl + 36; wb[4] = (u8)r; wb[5] = (u8)(r >> 8); wb[6] = (u8)(r >> 16); wb[7] = (u8)(r >> 24); }
                wb[40] = (u8)dl; wb[41] = (u8)(dl >> 8); wb[42] = (u8)(dl >> 16); wb[43] = (u8)(dl >> 24);
                dos_write((u16)h, wb, 44);
                while (done < dl) {
                    u32 chunk = dl - done > 4096 ? 4096 : dl - done;
                    for (k = 0; k < chunk; k += 4) *(volatile u32 *)(wb + k) = prd32(xaddr + done + k);
                    dos_write((u16)h, wb, (u16)chunk);
                    done += chunk;
                }
                dos_close(h);
                pr("Zapisano LOOP.WAV (%u KB) - przeslij go razem z USBTONE.LOG\n", (dl + 44) / 1024);
            }
        }
    }
    {   u32 h = xh; xms_call(0x0D00, &h, NULL); h = xh; xms_call(0x0A00, &h, NULL); }
    pr(bad ? "WYNIK: wykryto problemy (szczegoly wyzej)\n" : "WYNIK: OK - tor USB dziala bez zaklocen\n");
    return bad ? 1 : 0;
}
/* ------------------------------------------------------------------ */
/* argumenty                                                           */
/* ------------------------------------------------------------------ */
static int opt_auto, opt_c = -1, opt_p = -1, opt_scan, opt_loop, opt_dump, opt_info;
static u32 opt_k;   /* diagnostyka: zatrzymaj przejecie po kroku n */

static u32 parse_num(const char **s) { u32 v = 0; while (**s >= '0' && **s <= '9') v = v * 10 + (u32)(*(*s)++ - '0'); return v; }

static void parse_args(void)
{
    char cmd[128]; int n = *(volatile u8 *)0x80, i;
    const char *s;
    if (n > 126) n = 126;
    for (i = 0; i < n; i++) { char c = *(volatile char *)(0x81 + i); if (c >= 'a' && c <= 'z') c -= 32; cmd[i] = c; }
    cmd[n] = 0;
    s = cmd;
    while (*s) {
        if (*s == '/' || *s == '-') {
            s++;
            switch (*s++) {
            case 'A': opt_auto = 1; break;
            case 'V': verbose = 1; break;
            case 'S': opt_scan = 1; break;
            case 'L': if (*s == 'R') { s++; lr_test = 1; } else if (*s == 'O') { const char *q = s; while (*s >= 'A' && *s <= 'Z') s++; opt_loop = 1; if (s - q >= 6) loop_sim = 1; } else lr_test = 1; break;
            case 'R': if (*s == ':') s++; rate = parse_num(&s); break;
            case 'F': if (*s == ':') s++; tone_hz = parse_num(&s); break;
            case 'T': if (*s == ':') s++; seconds = parse_num(&s); break;
            case 'K': if (*s == ':') s++; opt_k = parse_num(&s); break;
            case 'D': opt_dump = 1; while (*s >= 'A' && *s <= 'Z') s++; break;
            case 'I': opt_k = 6; opt_info = 1; break;
            case 'G': if (*s == ':') s++; loop_db = parse_num(&s); if (loop_db > 60) loop_db = 60; break;
            case 'P': if (*s == ':') s++; opt_c = (int)parse_num(&s); if (*s == '.') { s++; opt_p = (int)parse_num(&s) - 1; } break;
            case '?': case 'H':
                pr("USBTONE " VERSION " - test karty USB Audio (PCM2902) przez OHCI\n"
                   "  /A        bez pytan: pierwszy port OHCI z urzadzeniem\n"
                   "  /P:k.p    uzyj kontrolera #k, portu p (numery z listy)\n"
                   "  /S        tylko skan PCI i portow (nic nie zmienia, bez opisu karty)\n"
                   "  /I        opis karty (deskryptory) do USBTONE.LOG, bez dzwieku\n"
                   "  /DUMP     zapisz pamiec BIOS (E0000-FFFFF) i tablice przerwan do BIOS.BIN\n"
                   "  /R:hz     czestotliwosc probkowania (domyslnie 48000)\n"
                   "  /F:hz     czestotliwosc tonu (domyslnie 1000)\n"
                   "  /T:s      czas w sekundach (domyslnie 10)\n"
                   "  /LR       naprzemiennie lewy/prawy kanal co 1 s\n"
                   "  /LOOP     pomiar w petli LINE OUT -> LINE IN (zapisuje LOOP.WAV)\n"
                   "  /G:n      poziom tonu w /LOOP: -n dBFS (domyslnie 30)\n"
                   "  /V        szczegoly na ekranie\n");
                asm volatile("movw $0x4C00,%%ax; int $0x21" ::: "eax");
            }
        } else s++;
    }
    if (rate < 8000 || rate > 96000) rate = 48000;
    if (tone_hz < 50 || tone_hz > 20000) tone_hz = 1000;
    if (seconds == 0 || seconds > 3600) seconds = 10;
}

/* ------------------------------------------------------------------ */
#include "rotate.h"
static int rot_n;
int main(void)
{
    u16 cs;
    int i, p, sel = -1, selp = -1, ls, act, rc;
    u8 dd[18], cfg9[9];
    static u8 cfg[1024];
    u16 total, mps0;

    asm volatile("movw %%cs,%0" : "=r"(cs));
    seg_base = (u32)cs << 4;
    gdtr.lim = 15; gdtr.base = seg_base + (u32)gdt;

    parse_args();
    {   /* poprzednie wyniki dostaja numer, zamiast byc nadpisane */
        static const char *const grp[3] = { "USBTONE.LOG", "LOOP.WAV", "BIOS.BIN" };
        rot_n = rot_group(grp, 3);
    }
    pr("USBTONE " VERSION " - test USB Audio przez OHCI (etap 1, ton testowy)\n");
    if (rot_n > 0) pr("Poprzednie wyniki zachowane z numerem %02u (USBTON%02u.LOG, LOOP%02u.WAV)\n", rot_n, rot_n, rot_n);
    if (rot_n < 0) pr("UWAGA: nie udalo sie zachowac poprzedniego USBTONE.LOG (numery do 99 zajete?) - zostanie nadpisany\n");

    if (in_v86()) {
        pr("BLAD: procesor pracuje w trybie V86 (EMM386/JEMM/QEMM?).\n"
           "Usun EMM386 z CONFIG.SYS (HIMEM moze zostac) i uruchom ponownie.\n");
        return 2;
    }
    /* A20 - potrzebne do adresow MMIO (bit 20) */
    outb(0x92, (u8)((inb(0x92) | 2) & ~1));
    unreal_fs();

    {   /* CPU */
        u32 a = 0, b, c, d, fl1, fl2;
        asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%0; pushl %0; popfl; pushfl; popl %0; pushl %1; popfl"
                     : "=&r"(fl1), "=&r"(fl2));
        if ((fl1 ^ fl2) & 0x200000) {
            char v[13];
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
            memcpy(v, &b, 4); memcpy(v + 4, &d, 4); memcpy(v + 8, &c, 4); v[12] = 0;
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
            lg("CPU: %s  sygnatura %08x  flagi %08x\n", v, a, d);
        } else lg("CPU: brak CPUID\n");
    }

    dma_phys = (seg_base + (u32)pool + 0xFF) & ~0xFFu;   /* 256 B wystarcza: pakiety nie przekraczaja stron 4 KB */
    lg("Segment %04x, pamiec DMA %08x-%08x\n", cs, dma_phys, dma_phys + DMA_USED);
    if (dma_phys + DMA_USED > seg_base + (u32)pool + sizeof pool || dma_phys + DMA_USED > 0xA0000) {
        pr("BLAD: zly uklad pamieci DMA\n"); return 2;
    }

    if (opt_dump) {
        /* BIOS.BIN: 0x00000-0x005FF (IVT + BDA), potem 0xC0000-0xFFFFF (BIOS karty VGA + BIOS) */
        int h = dos_create("BIOS.BIN");
        u8 *chunk = pool;   /* bufor DMA jeszcze nieuzywany */
        u32 a, k;
        if (h < 0) { pr("Nie mozna utworzyc BIOS.BIN\n"); return 1; }
        for (k = 0; k < 0x600; k += 4) *(u32 *)(chunk + (k & 1023)) = prd32(k), ((k & 1023) == 1020) ? (void)dos_write((u16)h, chunk, 1024) : (void)0;
        dos_write((u16)h, chunk, 0x600 & 1023);
        for (a = 0xC0000; a < 0x100000; a += 1024) {
            for (k = 0; k < 1024; k += 4) *(u32 *)(chunk + k) = prd32(a + k);
            dos_write((u16)h, chunk, 1024);
        }
        dos_close(h);
        pr("Zapisano BIOS.BIN (%u KB): IVT+BDA, potem C0000-FFFFF. Przeslij go.\n", (0x600 + 0x40000) / 1024);
        return 0;
    }
    if (loop_sim) { struct alt fa; int r; memset(&fa, 0, sizeof fa); r = loop_test(1, &fa, &fa); log_flush(); return r; }
    pci_scan();
    if (!nhc) { pr("Nie znaleziono kontrolerow USB OHCI/EHCI na PCI.\n"); log_flush(); return 3; }
    for (i = 0; i < nhc; i++) if (hcs[i].ehci)
        lg("EHCI #%d: HCSPARAMS=%08x CONFIGFLAG=%08x USBCMD=%08x USBSTS=%08x\n", i,
           prd32(hcs[i].capbase + 4), prd32(hcs[i].mmio + 0x40), prd32(hcs[i].mmio), prd32(hcs[i].mmio + 4));
    pr("Stan portow:\n");
    scan_ports(port_map, 1);
    for (i = 0; i < nhc; i++) for (p = 0; p < hcs[i].nports; p++)
        if ((port_map[i] >> p) & 1) pr("  #%d %s port %d: urzadzenie podlaczone\n", i, hcs[i].ehci ? "EHCI" : "OHCI", p + 1);
    log_flush();
    if (opt_scan) {
        /* diagnostyka przerwan: linie IRQ kontrolerow, tryb wyzwalania (ELCR), maski PIC, wektory */
        u16 elcr = (u16)(inb(0x4D0) | (inb(0x4D1) << 8));
        pr("Przerwania:\n");
        for (i = 0; i < nhc; i++) {
            u32 r3c = pci_rd(hcs[i].bus, hcs[i].dev, hcs[i].fn, 0x3C);
            u32 irq = r3c & 0xFF, pin = (r3c >> 8) & 0xFF;
            pr("  #%d %s IRQ %u (pin %c) %s", i, hcs[i].ehci ? "EHCI" : "OHCI", irq, pin ? 'A' + pin - 1 : '-',
               irq < 16 ? ((elcr >> irq) & 1 ? "poziom" : "ZBOCZE") : "");
            if (!hcs[i].ehci) pr("  HcControl=%08x IntEnable=%08x IntStatus=%08x", prd32(hcs[i].mmio + HcControl),
                                 prd32(hcs[i].mmio + HcInterruptEnable), prd32(hcs[i].mmio + HcInterruptStatus));
            else pr("  USBINTR=%08x", prd32(hcs[i].mmio + 8));
            if (irq < 16) { u32 v = irq < 8 ? 8 + irq : 0x70 + irq - 8; u32 vec = prd32(v * 4); pr("  wektor %x=%04x:%04x", v, vec >> 16, vec & 0xFFFF); }
            pr("\n");
        }
        pr("  ELCR=%04x  maska PIC1=%02x PIC2=%02x\n", elcr, inb(0x21), inb(0xA1));
        {   u32 v8 = prd32(8 * 4), v9 = prd32(9 * 4), v1c = prd32(0x1C * 4), v16 = prd32(0x16 * 4);
            pr("  INT 08=%04x:%04x 09=%04x:%04x 16=%04x:%04x 1C=%04x:%04x\n", v8 >> 16, v8 & 0xFFFF, v9 >> 16, v9 & 0xFFFF,
               v16 >> 16, v16 & 0xFFFF, v1c >> 16, v1c & 0xFFFF); }
        log_flush();
        pr("Tryb /S - koniec. Log: USBTONE.LOG\n"); return 0;
    }

    /* ---- wybor portu ---- */
    if (opt_c >= 0 && opt_c < nhc && opt_p >= 0) { sel = opt_c; selp = opt_p; }
    else if (opt_auto) {
        for (i = 0; i < nhc && sel < 0; i++) if (!hcs[i].ehci)
            for (p = 0; p < hcs[i].nports; p++) if ((port_map[i] >> p) & 1) { sel = i; selp = p; break; }
    } else {
        u32 a[MAXHC], b[MAXHC];
        pr("\nKROK 1: WYJMIJ karte PCM2902 z gniazda USB (pendrive zostaw).\n"
           "        Potem nacisnij ENTER  (ESC = wyjscie).\n");
        wait_enter(); wait_ticks(9);
        scan_ports(a, 0);
        pr("KROK 2: WLOZ karte PCM2902 do wolnego gniazda USB,\n"
           "        odczekaj ok. 3 sekundy i nacisnij ENTER.\n");
        wait_enter(); wait_ticks(9);
        scan_ports(b, 1);
        for (i = 0; i < nhc; i++) for (p = 0; p < hcs[i].nports; p++)
            if (((b[i] & ~a[i]) >> p) & 1) { if (sel < 0) { sel = i; selp = p; } pr("Nowe urzadzenie: #%d %s port %d\n", i, hcs[i].ehci ? "EHCI" : "OHCI", p + 1); }
        if (sel < 0) {
            pr("Nie wykryto nowego urzadzenia. Sprawdz kabel/gniazdo albo uzyj /P:k.p\n");
            log_flush(); return 4;
        }
        if (hcs[sel].ehci) {
            u32 c[MAXHC]; int e = sel, ep = selp;
            ehci_release(e, ep);
            scan_ports(c, 1);
            sel = -1;
            for (i = 0; i < nhc && sel < 0; i++) if (!hcs[i].ehci)
                for (p = 0; p < hcs[i].nports; p++) if (((c[i] & ~b[i]) >> p) & 1) { sel = i; selp = p; break; }
            if (sel < 0) { pr("Po przekazaniu portu EHCI urzadzenie nie pojawilo sie na OHCI.\n"); log_flush(); return 4; }
        }
    }
    if (sel < 0 || hcs[sel].ehci || selp >= hcs[sel].nports) { pr("Nie wybrano portu OHCI.\n"); log_flush(); return 4; }
    pr("Wybrano: #%d OHCI port %d\n", sel, selp + 1);
    {   int k2, firstohci = -1;
        for (k2 = 0; k2 < nhc; k2++) if (!hcs[k2].ehci) { firstohci = k2; break; }
        if (sel == firstohci)
            pr("UWAGA: to pierwszy kontroler USB (eBOX-2300SX: przednie LEWE gniazdo).\n"
               "BIOS potrzebuje jego przerwan do klawiatury USB - po tescie klawiatura USB\n"
               "przestanie dzialac. Lepiej wlozyc karte do przedniego PRAWEGO gniazda.\n");
    }
    ohci = hcs[sel].mmio;

    /* czy pendrive moze byc na tym samym kontrolerze? */
    for (p = 0; p < hcs[sel].nports; p++)
        if (p != selp && ((port_map[sel] >> p) & 1)) {
            pr("UWAGA: na tym kontrolerze jest inne urzadzenie (port %d) - jesli to pendrive\n"
               "z systemem, dysk przestanie dzialac. Log zostanie zapisany teraz.\n", p + 1);
            log_flush(); log_ok = 0;
        }
    log_flush();

    /* ---- przejecie ---- */
    stop_k = opt_k;
    {   int t = ohci_takeover(); if (t == 2) { log_flush(); return 0; } if (t) { log_flush(); return 5; } }
    if (port_reset(selp, &ls)) { log_flush(); return 6; }
    dev_ls = ls;

    /* ---- enumeracja ---- */
    rc = control(0, 8, 0x80, 6, 0x0100, 0, 8, dd, &act);
    if (rc) { pr("BLAD: GET_DESCRIPTOR(urzadzenie) na adresie 0\n"); goto fail; }
    mps0 = dd[7]; if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64) mps0 = 8;
    lg("bMaxPacketSize0=%u\n", mps0);
    if (port_reset(selp, &ls)) goto fail;
    if (control(0, mps0, 0x00, 5, 1, 0, 0, NULL, NULL)) { pr("BLAD: SET_ADDRESS\n"); goto fail; }
    wait_frames(10);
    if (control(1, mps0, 0x80, 6, 0x0100, 0, 18, dd, &act) || act < 18) { pr("BLAD: GET_DESCRIPTOR(urzadzenie)\n"); goto fail; }
    pr("Urzadzenie: VID %04x PID %04x  USB %x.%02x  klasa %02x\n", dd[8] | (dd[9] << 8), dd[10] | (dd[11] << 8),
       dd[3], dd[2], dd[4]);
    if ((dd[8] | (dd[9] << 8)) == 0x08BB && (dd[10] | (dd[11] << 8)) == 0x2902) pr("  -> to TI PCM2902\n");
    if ((dd[8] | (dd[9] << 8)) == 0x0D8C) pr("  -> C-Media (np. CM108/CM119) - obsluga testowa: przeslij USBTONE.LOG z /S\n");
    if (dd[15]) {   /* iProduct */
        u8 sd[64]; int k;
        if (!control(1, mps0, 0x80, 6, 0x0300 | dd[15], 0x0409, 64, sd, &act) && act > 2) {
            char nm[32]; int m = 0;
            for (k = 2; k + 1 < act && m < 31; k += 2) nm[m++] = (sd[k] >= 32 && sd[k] < 127) ? (char)sd[k] : '?';
            nm[m] = 0; pr("  Nazwa: %s\n", nm);
        }
    }
    if (control(1, mps0, 0x80, 6, 0x0200, 0, 9, cfg9, &act) || act < 9) { pr("BLAD: GET_DESCRIPTOR(konfiguracja)\n"); goto fail; }
    total = (u16)(cfg9[2] | (cfg9[3] << 8)); if (total > sizeof cfg) total = sizeof cfg;
    if (control(1, mps0, 0x80, 6, 0x0200, 0, total, cfg, &act)) { pr("BLAD: pelny deskryptor konfiguracji\n"); goto fail; }
    lg("Deskryptor konfiguracji (%d B):\n", act);
    for (i = 0; i < act; i++) lg("%02x%s", cfg[i], (i & 15) == 15 ? "\n" : " ");
    lg("\n");
    parse_config(cfg, act);
    if (control(1, mps0, 0x00, 9, cfg9[5], 0, 0, NULL, NULL)) { pr("BLAD: SET_CONFIGURATION\n"); goto fail; }
    pr("Konfiguracja %d ustawiona. Ustawien audio (OUT): %d\n", cfg9[5], nalts);
    if (opt_k == 6) {
        pr(opt_info ? "/I - opis karty zapisany w USBTONE.LOG (przeslij go). Uruchom komputer ponownie.\n"
                    : "/K:6 - zatrzymano po wyliczeniu urzadzenia\n");
        park_hc(); log_flush(); return 0;
    }

    {
        struct alt *best = NULL; int k;
        u32 try_rates[4]; try_rates[0] = rate; try_rates[1] = 48000; try_rates[2] = 44100; try_rates[3] = 32000;
        for (k = 0; k < 4 && !best; k++)
            for (i = 0; i < nalts; i++)
                if (alts[i].sub == 2 && (alts[i].nch == 2 || alts[i].nch == 1) && alt_has_rate(&alts[i], try_rates[k]) &&
                    (!best || alts[i].nch > best->nch)) { best = &alts[i]; rate = try_rates[k]; }
        if (!best) { pr("BLAD: brak obslugiwanego formatu (16 bit, 1-2 kanaly)\n"); goto fail; }
        if (best->mps < ((rate + 999) / 1000) * best->nch * 2) pr("UWAGA: MPS %u moze byc za maly\n", best->mps);
        pr("Wybrano: interfejs %d alt %d, EP %d OUT, MPS %u, %d kan., %u Hz, synchronizacja %s\n",
           best->ifn, best->alt, best->ep, best->mps, best->nch, rate,
           best->sync == 1 ? "async" : best->sync == 2 ? "adaptive" : best->sync == 3 ? "sync" : "brak");
        if (best->sync == 1) pr("UWAGA: endpoint asynchroniczny - bez sprzezenia zwrotnego moga byc trzaski\n");

        if (control(1, mps0, 0x01, 11, best->alt, best->ifn, 0, NULL, NULL)) { pr("BLAD: SET_INTERFACE\n"); goto fail; }
        {
            u8 f[3]; f[0] = (u8)rate; f[1] = (u8)(rate >> 8); f[2] = (u8)(rate >> 16);
            if (control(1, mps0, 0x22, 1, 0x0100, best->ep, 3, f, NULL)) lg("SET_CUR(czestotliwosc) odrzucone (moze byc OK)\n");
            if (!control(1, mps0, 0xA2, 0x81, 0x0100, best->ep, 3, f, &act) && act == 3)
                pr("Czestotliwosc w urzadzeniu: %u Hz\n", f[0] | (f[1] << 8) | ((u32)f[2] << 16));
        }
        for (k = 0; k < nfu && ac_if != 0xFF; k++) {
            u8 z = 0; u8 vol[2];
            int c;
            control(1, mps0, 0x21, 1, 0x0100, (u16)((fu_id[k] << 8) | ac_if), 1, &z, NULL);       /* mute off */
            for (c = 0; c <= 2; c++) {
                if (!control(1, mps0, 0xA1, 0x81, (u16)(0x0200 | c), (u16)((fu_id[k] << 8) | ac_if), 2, vol, &act) && act == 2)
                    lg("FU %d kanal %d glosnosc %d/256 dB\n", fu_id[k], c, (s16)(vol[0] | (vol[1] << 8)));
            }
        }
        log_flush();
        if (opt_loop) {
            struct alt *bi = NULL; int k2;
            for (k2 = 0; k2 < nalts_in; k2++)
                if (alts_in[k2].sub == 2 && alts_in[k2].nch == 2 && alt_has_rate(&alts_in[k2], 48000)) { bi = &alts_in[k2]; break; }
            if (!bi || rate != 48000 || best->nch != 2) { pr("BLAD: pomiar wymaga 48 kHz stereo na wyjsciu i wejsciu\n"); rc = 1; }
            else {
                u8 f[3]; f[0] = (u8)48000; f[1] = (u8)(48000 >> 8); f[2] = 0;
                pr("Wejscie: interfejs %d alt %d, EP %d IN, MPS %u\n", bi->ifn, bi->alt, bi->ep, bi->mps);
                if (control(1, mps0, 0x01, 11, bi->alt, bi->ifn, 0, NULL, NULL)) { pr("BLAD: SET_INTERFACE (wejscie)\n"); goto fail; }
                control(1, mps0, 0x22, 1, 0x0100, 0x80 | bi->ep, 3, f, NULL);
                log_flush();
                rc = loop_test(1, best, bi);
            }
        } else
        rc = stream(1, best);
    }
    pr(rc ? "Zakonczono z bledami transmisji.\n" : "Zakonczono bez bledow transmisji.\n");
    pr("Czy slychac bylo czysty ton? Wyslij plik USBTONE.LOG.\n");
    goto done;
fail:
    pr("Test przerwany - szczegoly w USBTONE.LOG\n");
    rc = 7;
done:
    owr(HcControl, orr(HcControl) & ~(CTL_PLE | CTL_IE | CTL_CLE));
    park_hc();
    if (!log_ok) {
        pr("Zapisac log na dysk? Jesli system jest na pendrive w tym samym kontrolerze,\n"
           "komputer moze sie zawiesic. (T/N)\n");
        for (;;) { u16 k = bios_getkey() & 0xFF; if (k == 't' || k == 'T') { log_ok = 1; break; } if (k == 'n' || k == 'N') break; }
    }
    log_flush();
    pr("Uruchom komputer ponownie przed kolejnym testem.\n");
    return rc;
}
