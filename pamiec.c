/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * PAMIEC.COM - pamiec w DOS: konwencjonalna (z lista blokow jak MEM /C), UMB, XMS,
 * EMS, VCPI i DPMI. Dziala pod FreeDOS i MS-DOS, w trybie rzeczywistym i V86.
 * Licencja: MIT.   PAMIEC /E = komunikaty po angielsku, /K = bez listy blokow.
 * PAMIEC /HIGH i /LOW: strategia przydzialu DOS (od gory / zwykla) - uzywane w AUTOEXEC
 * wokol SBEMU, zeby jego bloki nie dzielily wolnej pamieci.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed int s32;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end

void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static u8 peekb(u16 seg, u16 off) { u8 v; asm volatile("movw %1,%%fs\n\tmovb %%fs:(%2),%0" : "=q"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u16 peekw(u16 seg, u16 off) { u16 v; asm volatile("movw %1,%%fs\n\tmovw %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }

static int fmt(char *o, int max, const char *f, va_list ap)
{
    int n = 0; static const char hx[] = "0123456789ABCDEF";
    while (*f) {
        char c = *f++;
        if (c != '%') { if (n < max - 1) o[n++] = c; continue; }
        { int zero = 0, width = 0, left = 0; char tmp[16]; int tl = 0; const char *s = tmp; int sl, i;
          if (*f == '-') { left = 1; f++; }
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
          if (!left) while (sl < width && n < max - 1) { o[n++] = zero ? '0' : ' '; width--; }
          for (i = 0; i < sl && n < max - 1; i++) o[n++] = s[i];
          if (left) while (sl < width && n < max - 1) { o[n++] = ' '; width--; } }
    }
    o[n] = 0; return n;
}
static void pr(const char *f, ...)
{
    char b[256], o[512]; va_list ap; int n, i, m = 0;
    va_start(ap, f); n = fmt(b, sizeof b, f, ap); va_end(ap);
    for (i = 0; i < n; i++) { if (b[i] == '\n') o[m++] = '\r'; o[m++] = b[i]; }
    dos_write(1, o, (u16)m);
}
static int en;
#define T(pl, eng) (en ? (eng) : (pl))

/* XMS */
static struct { u16 off, seg; } __attribute__((packed)) xms_entry;
static int xms_init(void)
{
    u32 ax = 0x4300, b = 0, es_ = 0;
    asm volatile("int $0x2F" : "+a"(ax) :: "memory", "cc");
    if ((ax & 0xFF) != 0x80) return 0;
    ax = 0x4310;
    asm volatile("pushw %%es\n\tint $0x2F\n\txorl %%ecx,%%ecx\n\tmovw %%es,%%cx\n\tpopw %%es" : "+a"(ax), "=b"(b), "=c"(es_) :: "memory", "cc");
    xms_entry.off = (u16)b; xms_entry.seg = (u16)es_;
    return 1;
}
static u32 xms_call(u32 ax, u32 *dx, u32 *bx)
{
    u32 a = ax, d = dx ? *dx : 0, b = bx ? *bx : 0;
    asm volatile("lcallw *%3" : "+a"(a), "+d"(d), "+b"(b) : "m"(xms_entry) : "memory", "cc", "ecx", "esi", "edi");
    if (dx) { *dx = d; }
    if (bx) { *bx = b; }
    return a;
}

static void mcb_name(u16 mcb, u16 owner, char *o)
{
    int i;
    if (owner == 0) { memcpy(o, T("(wolne)", "(free)"), 8); return; }
    if (owner == 8) { memcpy(o, "DOS", 4); for (i = 0; i < 2; i++) { u8 c = peekb(mcb, 8 + i); if (c == 'S' && i == 0 && peekb(mcb, 9) == 'D') { memcpy(o, "DOS/SD", 7); return; } } return; }
    if (owner == mcb + 1 || 1) {
        u16 own = (u16)(owner - 1);
        for (i = 0; i < 8; i++) { u8 c = peekb(own, 8 + i); if (c < 32 || c > 126) break; o[i] = (char)c; }
        o[i] = 0;
        if (!i) memcpy(o, "?", 2);
        if (owner != mcb + 1) { int l = str_len(o); memcpy(o + l, T(" (dane)", " (data)"), 8); }
    }
}

static u32 fr_lo, fr_hi, lg_lo, lg_hi; static int any_hi;
static void walk(u16 start, int list)
{
    u16 m = start; int n = 0, hdr = 0;
    while (n < 400) {
        u8 sig = peekb(m, 0); u16 owner = peekw(m, 1), size = peekw(m, 3);
        char name[24];
        int hi = m >= 0x9FFF;
        if (sig != 'M' && sig != 'Z') break;
        if (hi && !hdr) { any_hi = 1; hdr = 1; if (list) pr(T("\n  Pamiec gorna (UMB):\n", "\n  Upper memory (UMB):\n")); }
        if (!owner || (hi && owner == 8 && size < 2)) { }
        if (!owner) { u32 b = (u32)size * 16; if (hi) { fr_hi += b; if (b > lg_hi) lg_hi = b; } else { fr_lo += b; if (b > lg_lo) lg_lo = b; } }
        if (list) { mcb_name(m, owner, name); pr("  %04x  %6u  %s\n", (u32)m, (u32)size * 16, name); }
        n++;
        if (sig == 'Z') break;
        m = (u16)(m + size + 1);
    }
}

int main(void)
{
    int n = *(volatile u8 *)0x80, i, list = 1;
    u32 conv, ax, bx = 0, dx;
    u16 first;
    {   char cmd[128]; int st = -1;
        for (i = 0; i < n && i < 126; i++) cmd[i] = *(volatile char *)(0x81 + i) | 32;
        cmd[i] = 0;
        for (i = 0; cmd[i]; i++) if (cmd[i] == '/') {
            if (cmd[i+1] == 'e') en = 1;
            else if (cmd[i+1] == 'k') list = 0;
            else if (cmd[i+1] == 'h' && cmd[i+2] == 'i') st = 2;     /* /HIGH: przydzial od gory */
            else if (cmd[i+1] == 'l' && cmd[i+2] == 'o') st = 0;     /* /LOW: zwykly (first fit) */
        }
        /* /HIGH przed SBEMU: wszystko, co SBEMU zostawia w pamieci konwencjonalnej, trafia na
           gore i nie dzieli wolnej pamieci; /LOW po SBEMU przywraca zwykla strategie DOS */
        if (st >= 0) { u32 a = 0x5801, b = (u32)st; asm volatile("int $0x21" : "+a"(a), "+b"(b) :: "cc"); return 0; }
    }
    asm volatile("int $0x12" : "=a"(ax) :: "cc"); conv = ax & 0xFFFF;
    {   u32 a = 0x5200, b = 0, es_ = 0;
        asm volatile("pushw %%es\n\tint $0x21\n\txorl %%ecx,%%ecx\n\tmovw %%es,%%cx\n\tpopw %%es" : "+a"(a), "=b"(b), "=c"(es_) :: "memory", "cc");
        first = peekw((u16)es_, (u16)(b - 2)); }
    pr(T("PAMIEC - pamiec w DOS\n", "PAMIEC - DOS memory report\n"));
    pr(T("\nPamiec konwencjonalna: %u KB (BIOS)\n", "\nConventional memory: %u KB (BIOS)\n"), conv);
    if (list) pr(T("  MCB   bajtow  wlasciciel\n", "  MCB    bytes  owner\n"));
    {   u32 g = 0x5802, old, st = 0x5803, b1 = 1;
        asm volatile("int $0x21" : "+a"(g) :: "cc"); old = g & 0xFF;
        asm volatile("int $0x21" : "+a"(st), "+b"(b1) :: "cc");      /* dolacz UMB do lancucha */
        walk(first, list);
        st = 0x5803; b1 = old; asm volatile("int $0x21" : "+a"(st), "+b"(b1) :: "cc");
    }
    {   /* ten program dostal od DOS najwiekszy wolny blok - jego rozmiar = miejsce dla programu */
        u16 me; u32 own, big;
        asm volatile("movw %%cs,%0" : "=r"(me));
        own = (u32)peekw((u16)(me - 1), 3) * 16;
        big = own > lg_lo ? own : lg_lo;
        pr(T("\nDla programow (najwiekszy blok): %u B = %u KB\n", "\nFor programs (largest block): %u B = %u KB\n"), big, big / 1024);
        pr(T("Pozostale wolne bloki: %u B\n", "Other free blocks: %u B\n"), fr_lo);
    }
    if (any_hi) pr(T("UMB: wolne %u B, najwiekszy blok %u B\n", "UMB: free %u B, largest block %u B\n"), fr_hi, lg_hi);
    else pr(T("UMB: brak (DOS=UMB nie wlaczone albo brak menedzera UMB)\n", "UMB: none (DOS=UMB not set or no UMB provider)\n"));
    {   u32 a = 0x3306, b = 0, d = 0; asm volatile("int $0x21" : "+a"(a), "=b"(b), "=d"(d) :: "cc");
        pr(T("DOS w HMA: %s\n", "DOS in HMA: %s\n"), (d >> 8) & 0x10 ? T("tak", "yes") : T("nie", "no")); }

    /* XMS */
    if (xms_init()) {
        u32 v = xms_call(0x0000, 0, &bx) & 0xFFFF, fr16 = 0, tot16 = 0;
        dx = 0; ax = xms_call(0x0800, &dx, 0); fr16 = ax & 0xFFFF; tot16 = dx & 0xFFFF;
        pr(T("\nXMS %u.%02x: najwiekszy blok %u KB, wolne %u KB (funkcja 08h, ograniczenie 64 MB)\n",
             "\nXMS %u.%02x: largest block %u KB, free %u KB (function 08h, max 64 MB)\n"), (v >> 8) & 0xFF, v & 0xFF, fr16, tot16);
        if (((v >> 8) & 0xFF) >= 3) {
            u32 a = 0x8800, b = 0, c = 0, d = 0;
            asm volatile("lcallw *%4" : "+a"(a), "=b"(b), "=c"(c), "=d"(d) : "m"(xms_entry) : "memory", "cc", "esi", "edi");
            if ((b & 0xFF) == 0) pr(T("  XMS 3.0: najwiekszy blok %u KB, wolne %u KB\n", "  XMS 3.0: largest block %u KB, free %u KB\n"), a, d);
        }
    } else pr(T("\nXMS: brak (nie ma HIMEM/HIMEMX/JEMMEX)\n", "\nXMS: not present (no HIMEM/HIMEMX/JEMMEX)\n"));

    /* EMS */
    {   u16 vs = peekw(0, 0x67 * 4 + 2); static const char nm[] = "EMMXXXX0"; int k, emm = 1;
        for (k = 0; k < 8; k++) if (peekb(vs, (u16)(10 + k)) != (u8)nm[k]) emm = 0;
        if (!emm) { static const char nm2[] = "EMMQXXX0"; emm = 2; for (k = 0; k < 8; k++) if (peekb(vs, (u16)(10 + k)) != (u8)nm2[k]) emm = 0; }
        if (emm == 1) {
            u32 a = 0x4200, b = 0, d = 0; asm volatile("int $0x67" : "+a"(a), "=b"(b), "=d"(d) :: "cc");
            u32 f = 0x4100, fb = 0; asm volatile("int $0x67" : "+a"(f), "=b"(fb) :: "cc");
            if ((a & 0xFF00) == 0)
                pr(T("\nEMS: jest, ramka stron %04x, wolne %u KB z %u KB\n", "\nEMS: present, page frame %04x, free %u KB of %u KB\n"),
                   (f & 0xFF00) ? 0 : (fb & 0xFFFF), (b & 0xFFFF) * 16, (d & 0xFFFF) * 16);
            else pr(T("\nEMS: menedzer jest, ale EMS wylaczone (NOEMS)\n", "\nEMS: manager present, but EMS is disabled (NOEMS)\n"));
        } else if (emm == 2) pr(T("\nEMS: menedzer jest, ale EMS wylaczone (NOEMS)\n", "\nEMS: manager present, but EMS is disabled (NOEMS)\n"));
        else pr(T("\nEMS: brak\n", "\nEMS: not present\n"));
        if (emm) {
            u32 a = 0xDE00, b = 0; asm volatile("int $0x67" : "+a"(a), "=b"(b) :: "cc");
            pr(T("VCPI: %s\n", "VCPI: %s\n"), (a & 0xFF00) == 0 ? T("jest", "present") : T("brak", "not present"));
        }
    }
    /* DPMI */
    {   u32 a = 0x1687, b = 0, d = 0; asm volatile("pushw %%es\n\tint $0x2F\n\tpopw %%es" : "+a"(a), "=b"(b), "=d"(d) :: "memory", "cc", "ecx", "esi", "edi");
        if ((a & 0xFFFF) == 0) pr(T("DPMI: jest (wersja %u.%u)\n", "DPMI: present (version %u.%u)\n"), (d >> 8) & 0xFF, d & 0xFF);
        else pr(T("DPMI: brak\n", "DPMI: not present\n")); }
    return 0;
}
