/*
 * Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic).
 * License: MIT - see LICENSE.TXT (PL: LICENCJA.TXT).
 *
 * rotate.h - zachowanie poprzednich wynikow przed zapisem nowych (licencja MIT).
 * Wymaga typow u8/u16/u32 i dzialania w .COM (DS = ES = CS).
 *
 * rot_group(): jesli istnieje ktorykolwiek z plikow grupy, wszystkie istniejace
 * dostaja ten sam kolejny numer: RECOUT.WAV -> RECOUT03.WAV, RECIN.WAV -> RECIN03.WAV.
 * Podstawa nazwy jest skracana do 6 znakow (8.3): USBTONE.LOG -> USBTON03.LOG.
 * Numer = najwyzszy istniejacy w grupie + 1 (01..99), wiec kolejnosc odpowiada czasowi.
 */
#ifndef ROTATE_H
#define ROTATE_H

static int rot_exists(const char *n)
{
    u32 ax = 0x4300; u8 cf;
    asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "d"(n) : "memory", "cc", "ecx");
    return !cf;
}
static int rot_rename(const char *from, const char *to)
{
    u32 ax = 0x5600; u8 cf;
    asm volatile("int $0x21; setc %1" : "+a"(ax), "=qm"(cf) : "d"(from), "D"(to) : "memory", "cc");
    return !cf;
}
static void rot_name(char *o, const char *name, int n)
{
    int i = 0, k = 0;
    while (name[i] && name[i] != '.' && k < 6) o[k++] = name[i++];
    while (name[i] && name[i] != '.') i++;
    o[k++] = (char)('0' + n / 10); o[k++] = (char)('0' + n % 10);
    while (name[i] && k < 12) o[k++] = name[i++];
    o[k] = 0;
}
/* zwraca numer (1..99), 0 = nie bylo czego zachowac, -1 = brak wolnego numeru/blad */
static int rot_group(const char *const *names, int cnt)
{
    char t[16];
    int i, n, top = 0, any = 0;
    for (i = 0; i < cnt; i++) if (rot_exists(names[i])) any = 1;
    if (!any) return 0;
    for (n = 1; n <= 99; n++)
        for (i = 0; i < cnt; i++) { rot_name(t, names[i], n); if (rot_exists(t)) top = n; }
    if (top >= 99) return -1;
    n = top + 1;
    for (i = 0; i < cnt; i++)
        if (rot_exists(names[i])) { rot_name(t, names[i], n); if (!rot_rename(names[i], t)) return -1; }
    return n;
}
/* wolne miejsce na biezacym dysku w bajtach (0xFFFFFFFF przy bledzie) */
static u32 rot_free_bytes(void)
{
    u32 ax = 0x3600, bx = 0, cx = 0, dx = 0;
    asm volatile("int $0x21" : "+a"(ax), "+b"(bx), "+c"(cx), "+d"(dx) :: "memory", "cc");
    if ((ax & 0xFFFF) == 0xFFFF) return 0xFFFFFFFFu;
    return (ax & 0xFFFF) * (bx & 0xFFFF) * (cx & 0xFFFF);
}
#endif
