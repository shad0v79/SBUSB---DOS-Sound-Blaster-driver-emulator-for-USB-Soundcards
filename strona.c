/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * STRONA.COM - pokazuje plik tekstowy strona po stronie (zamiast MORE, ktorego nie ma
 * w podstawowym FreeDOS). STRONA plik.txt   Licencja: MIT.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
static int dos_write(u16 h, const void *buf, u16 len)
{ u32 ax = 0x4000; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_read(u16 h, void *buf, u16 len)
{ u32 ax = 0x3F00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "b"((u32)h), "c"((u32)len), "d"(buf) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static int dos_open(const char *n)
{ u32 ax = 0x3D00; u8 cf; asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "c"(0), "d"(n) : "memory", "cc"); return cf ? -1 : (int)(ax & 0xFFFF); }
static u16 getkey(void) { u32 ax = 0; asm volatile("int $0x16" : "+a"(ax) :: "memory", "cc"); return (u16)ax; }
static void outs(const char *s) { int n = 0; while (s[n]) n++; dos_write(1, s, (u16)n); }
static u8 buf[4096];
int main(void)
{
    char name[80]; int n = *(volatile u8 *)0x80, i, k = 0, h, lines = 0, col = 0;
    for (i = 0; i < n && k < 79; i++) { char c = *(volatile char *)(0x81 + i); if (c != ' ' && c != '\t') name[k++] = c; }
    name[k] = 0;
    if (!k) { outs("STRONA plik.txt - pokazuje plik strona po stronie / shows a file page by page\r\n"); return 1; }
    h = dos_open(name);
    if (h < 0) { outs("Nie mozna otworzyc pliku / cannot open file\r\n"); return 2; }
    for (;;) {
        int r = dos_read((u16)h, buf, sizeof buf);
        if (r <= 0) break;
        for (i = 0; i < r; i++) {
            u8 c = buf[i];
            if (c == 26) return 0;
            dos_write(1, &c, 1);
            if (c == '\n') { lines++; col = 0; }
            else if (c != '\r' && ++col >= 80) { lines++; col = 0; }
            if (lines >= 23) {
                u8 key;
                outs("-- dalej / more: dowolny klawisz / any key, Esc = koniec / quit --");
                key = (u8)getkey();
                outs("\r                                                                     \r");
                if (key == 27) return 0;
                lines = 0;
            }
        }
    }
    return 0;
}
