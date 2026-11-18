/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * KBTEST.COM - automatyczny test klawiatury USB z restartami (eBOX-2300SX).
 *   KBTEST START   - rozpocznij serie (etap 1, menu startowe -> opcja 4)
 *   KBTEST GET     - errorlevel = numer etapu (1..7), 255 = koniec
 *   KBTEST ASK     - czekaj 15 s na klawisz, zapisz wynik, nastepny etap, restart
 *   KBTEST DONE    - przywroc menu (opcja 1), pokaz wyniki
 * Wyniki: KBRESULT.TXT. Licencja: MIT.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static u16 peekw(u16 seg, u16 off) { u16 v; asm volatile("movw %1,%%fs\n\tmovw %%fs:(%2),%0" : "=r"(v) : "r"(seg), "r"((u32)off) : "memory"); return v; }
static u16 ticks(void) { return peekw(0x40, 0x6C); }

static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_read(u16 h, void *buf, u16 len)
{ u32 ax = 0x3F00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_open(const char *n, int mode)   /* 0 = otworz r/w, 1 = utworz */
{ u32 ax = mode ? 0x3C00 : 0x3D02; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static void dos_close(int h) { u32 ax = 0x3E00; asm volatile("int $0x21" : "+a"(ax) : "b"((u32)h) : "memory", "cc"); }
static void dos_seek(int h, u16 how, u32 pos) { u32 ax = 0x4200 | how, cx = pos >> 16, dx = pos & 0xFFFF; asm volatile("int $0x21" : "+a"(ax), "+c"(cx), "+d"(dx) : "b"((u32)h) : "memory", "cc"); }
static void dos_delete(const char *n) { u32 ax = 0x4100; asm volatile("int $0x21" : "+a"(ax) : "d"(n) : "memory", "cc"); }
static int kbhit(void) { u32 ax = 0x0100; u8 nz; asm volatile("int $0x16; setnz %1" : "+a"(ax), "=qm"(nz) :: "memory", "cc"); return nz; }
static u16 getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }
static void puts_(const char *s) { int i, n = str_len(s); for (i = 0; i < n; i++) { if (s[i] == '\n') dos_write(1, "\r\n", 2); else dos_write(1, &s[i], 1); } }

#define NSTAGES 7
static const char *names_r[NSTAGES + 1] = { "",
    "R1 USBTONE /S (tylko skan)",
    "R2 USBTONE /K:1 (wylaczenie przerwan)",
    "R3 USBTONE /K:2 (stan USBRESET)",
    "R4 USBTONE /K:3 (reset HCR)",
    "R5 USBTONE /K:4 (HCCA, OPERATIONAL)",
    "R6 USBTONE /K:5 (root hub, zasilanie)",
    "R7 USBTONE pelny (ton 3 s)" };
static char series = 'J';
static const char *names[NSTAGES + 1] = { "",
    "1 kontrola: JEMMEX+HDPMI, bez SBEMU",
    "2 SBEMU, stop przed EHCI (S2)",
    "3 SBEMU, stop po EHCI (S3)",
    "4 SBEMU, stop po odczycie rejestrow (S4)",
    "5 SBEMU, stop po sprawdzeniu BIOS/PCI (S5)",
    "6 SBEMU, stop po przejeciu kontrolera (S6)",
    "7 SBEMU pelny (karta dziala)" };

static int get_stage(void)
{
    char b[8]; int h = dos_open("KBSTAGE.TXT", 0), n, v = 0, i;
    if (h < 0) return 0;
    n = dos_read((u16)h, b, sizeof b); dos_close(h);
    i = 0;
    if (n > 0 && b[0] >= 'A') { series = b[0]; i = 1; }
    for (; i < n && b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + (b[i] - '0');
    return v;
}
static void set_stage(int v)
{
    char b[4]; int h = dos_open("KBSTAGE.TXT", 1), n = 0;
    b[n++] = series;
    if (v >= 10) b[n++] = (char)('0' + v / 10);
    b[n++] = (char)('0' + v % 10);
    if (h >= 0) { dos_write((u16)h, b, (u16)n); dos_close(h); }
}
/* MENUDEFAULT=x,y w FDCONFIG.SYS - podmiana 3 znakow w miejscu */
static void set_menu(char opt, char delay)
{
    static char buf[1024]; int h = dos_open("\\FDCONFIG.SYS", 0), n, i;
    if (h < 0) { puts_("Brak \\FDCONFIG.SYS\n"); return; }
    n = dos_read((u16)h, buf, sizeof buf);
    for (i = 0; i + 15 <= n; i++) {
        const char *k = "MENUDEFAULT="; int j;
        for (j = 0; j < 12 && buf[i + j] == k[j]; j++) ;
        if (j == 12) {
            buf[i + 12] = opt; buf[i + 13] = ','; buf[i + 14] = delay;
            dos_seek(h, 0, (u32)(i + 12)); dos_write((u16)h, buf + i + 12, 3);
            break;
        }
    }
    dos_close(h);
}
static void append_result(const char *line)
{
    int h = dos_open("KBRESULT.TXT", 0);
    if (h < 0) h = dos_open("KBRESULT.TXT", 1);
    if (h < 0) return;
    dos_seek(h, 2, 0);
    dos_write((u16)h, line, (u16)str_len(line));
    dos_write((u16)h, "\r\n", 2);
    dos_close(h);
}
static void reboot(void)
{
    int i;
    puts_("Restart...\n");
    for (i = 0; i < 100000 && (inb(0x64) & 2); i++) ;
    outb(0x64, 0xFE);                         /* reset przez kontroler klawiatury */
    for (i = 0; i < 1000000; i++) inb(0x80);
    outb(0x92, inb(0x92) | 1);                /* szybki reset */
    for (i = 0; i < 1000000; i++) inb(0x80);
    asm volatile("ljmpw $0xFFFF,$0x0000");
}

int main(void)
{
    char cmd[16]; int n = *(volatile u8 *)0x80, i, k = 0;
    for (i = 0; i < n && k < 15; i++) { char c = *(volatile char *)(0x81 + i); if (c != ' ') cmd[k++] = (char)(c & ~32); }
    cmd[k] = 0;

    if (cmd[0] == 'S') {          /* START [R] */
        series = (cmd[5] == 'R') ? 'R' : 'J';
        dos_delete("KBRESULT.TXT");
        append_result(series == 'R' ? "Automatyczny test klawiatury USB - seria R (tryb rzeczywisty, USBTONE)" : "Automatyczny test klawiatury USB");
        set_stage(1);
        set_menu(series == 'R' ? '5' : '4', '1');
        puts_("Seria testow rozpoczeta. Komputer bedzie sie sam restartowal.\n");
        return 0;
    }
    if (cmd[0] == 'G') {          /* GET */
        int s = get_stage();
        return (s < 1 || s > NSTAGES) ? 255 : s;
    }
    if (cmd[0] == 'A') {          /* ASK */
        int s = get_stage(); u16 t0, last = 0xFFFF;
        char line[96]; const char *nm = (s >= 1 && s <= NSTAGES) ? (series == 'R' ? names_r[s] : names[s]) : "?";
        int ok = 0, j; u8 busy = (u8)peekw(0xE623, 0x32);
        while (kbhit()) getkey();
        puts_("\n=====================================================\n");
        puts_(" ETAP "); puts_(nm); puts_("\n");
        puts_(" NACISNIJ DOWOLNY KLAWISZ NA KLAWIATURZE USB\n");
        puts_(" (brak reakcji przez 15 s = klawiatura nie dziala)\n");
        puts_("=====================================================\n");
        t0 = ticks();
        while ((u16)(ticks() - t0) < 273) {               /* 15 s */
            u16 left = (u16)(15 - (u16)(ticks() - t0) / 18);
            if (left != last) { char c[6]; c[0] = '\r'; c[1] = ' '; c[2] = (char)('0' + left / 10); c[3] = (char)('0' + left % 10); c[4] = ' '; c[5] = 's'; dos_write(1, c, 6); last = left; }
            if (kbhit()) { getkey(); ok = 1; break; }
        }
        puts_(ok ? "\n -> DZIALA\n" : "\n -> NIE DZIALA (brak klawisza)\n");
        for (j = 0; nm[j]; j++) line[j] = nm[j];
        { const char *r = ok ? ": DZIALA" : ": NIE DZIALA"; int q; for (q = 0; r[q]; q++) line[j++] = r[q]; }
        { static const char hx[] = "0123456789ABCDEF"; const char *r = "  (flaga BIOS USB "; int q; for (q = 0; r[q]; q++) line[j++] = r[q];
          line[j++] = hx[busy >> 4]; line[j++] = hx[busy & 15]; line[j++] = ')'; }
        line[j] = 0;
        append_result(line);
        set_stage(s + 1);
        { u16 t1 = ticks(); while ((u16)(ticks() - t1) < 36) ; }
        reboot();
        return 0;
    }
    if (cmd[0] == 'D') {          /* DONE */
        set_menu('1', '5');
        dos_delete("KBSTAGE.TXT");
        puts_("\nSeria zakonczona. Wyniki (KBRESULT.TXT):\n\n");
        {   static char b[2048]; int h = dos_open("KBRESULT.TXT", 0), m;
            if (h >= 0) { m = dos_read((u16)h, b, sizeof b - 1); dos_close(h); if (m > 0) dos_write(1, b, (u16)m); } }
        puts_("\nPrzeslij plik KBRESULT.TXT. Menu startowe wrocilo do opcji 1.\n");
        return 0;
    }
    puts_("KBTEST START | GET | ASK | DONE\n");
    return 0;
}
