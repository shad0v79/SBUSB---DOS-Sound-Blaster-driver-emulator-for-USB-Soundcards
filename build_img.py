#!/usr/bin/env python3
# Buduje obraz dysku USB (MBR + partycja FAT16 + FreeDOS + USBTONE)
# Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic). MIT.
import os, struct, subprocess, sys
out = sys.argv[1]; autoexec = sys.argv[2] if len(sys.argv) > 2 else 'files/AUTOEXEC.BAT'
FD = '/home/claude/ext/rufus/res/freedos'
SPT, HEADS = 63, 255
total = 30720  # 15 MiB - miesci sie na kazdej karcie CF '16 MB'
start = 63; psize = total - start
part = '/tmp/part.img'
if os.path.exists(part): os.remove(part)
subprocess.check_call(['mkfs.fat', '-C', '-F', '16', '-h', str(start), '-g', f'{HEADS}/{SPT}',
                       '-s', '4', '-S', '512', '-M', '0xF8', '-D', '0x80', part, str(psize // 2)], stdout=subprocess.DEVNULL)
# sektor startowy FreeDOS + BPB z mkfs
bs = bytearray(open(part, 'rb').read(512)); fb = open('boot16.bin', 'rb').read()
nb = bytearray(fb); nb[3:0x3E] = bs[3:0x3E]
open(part, 'r+b').write(nb)
env = dict(os.environ, MTOOLS_SKIP_CHECK='1')
def mc(*a): subprocess.check_call(['mcopy', '-i', part, *a], env=env)
# Dwa pierwsze wpisy katalogu glownego zarezerwowane (puste, ukryte) na IO.SYS i MSDOS.SYS:
# MS-DOS 6.22 SYS wymaga, zeby te pliki byly pierwszymi wpisami. FreeDOS ich nie uzywa.
open('/tmp/empty.bin', 'wb').close()
mc('/tmp/empty.bin', '::IO.SYS'); mc('/tmp/empty.bin', '::MSDOS.SYS')
subprocess.check_call(['mattrib', '-i', part, '+h', '+s', '+r', '::IO.SYS', '::MSDOS.SYS'], env=env)
mc(f'{FD}/KERNEL.SYS', '::KERNEL.SYS')
mc(f'{FD}/COMMAND.COM', '::COMMAND.COM')
mc('files/FDCONFIG.SYS', '::FDCONFIG.SYS')   # FreeDOS
mc('files/CONFIG.SYS', '::CONFIG.SYS')       # MS-DOS 6.22 / 7.1 (FreeDOS czyta FDCONFIG.SYS)
mc('/home/claude/ext/hx/HimemX.exe', '::HIMEMX.EXE')
mc(autoexec, '::AUTOEXEC.BAT')
mc('USBTONE.COM', '::USBTONE.COM')
mc('CPUTEST.COM', '::CPUTEST.COM')
mc('USBREC.COM', '::USBREC.COM')
mc('files/CZYTAJ.TXT', '::CZYTAJ.TXT')
mc('files/README.TXT', '::README.TXT')
mc('files/POMOC.BAT', '::POMOC.BAT'); mc('files/HELP.BAT', '::HELP.BAT')
mc('STRONA.COM', '::STRONA.COM')
subprocess.check_call(['mmd', '-i', part, '::ZRODLA'], env=env)
for f in ['main.c', 'cputest.c', 'sbtest.c', 'usbrec.c', 'sbmix.c', 'usbmix.c', 'pamiec.c', 'strona.c', 'install.c', 'rotate.h', 'dbtab.h', 'sine.h', 'start.S', 'link.ld', 'build.sh', 'mbr.asm', 'build_img.py']:
    up = f.upper().replace('BUILD_IMG.PY', 'BUILDIMG.PY')
    mc(f, '::ZRODLA/' + up)
# gra Alien Carnage (freeware 2007, kopiowana bez zmian)
import glob
GAME = '/home/claude/game_in/ac'
if os.path.isdir(GAME):
    subprocess.check_call(['mmd', '-i', part, '::CARNAGE'], env=env)
    for f in sorted(glob.glob(GAME + '/*')):
        mc(f, '::CARNAGE/' + os.path.basename(f).upper())
    mc('files/GRA.BAT', '::GRA.BAT')
    mc('files/GRACFG.BAT', '::GRACFG.BAT')
# SBEMU (GPL) + JEMMEX, JLOAD, QPIEMU, HDPMI32i z wydania SBEMU 1.0.0-beta.5
SB = '/home/claude/ext/sbemu_rel/SBEMU'
subprocess.check_call(['mmd', '-i', part, '::SBEMU'], env=env)
for f in ['HDPMI32i.EXE', 'JEMMEX.EXE', 'JLOAD.EXE', 'QPIEMU.DLL', 'RELEASE_NOTES.md']:
    mc(SB + '/' + f, '::SBEMU/' + ('RELNOTES.TXT' if f.endswith('.md') else f.upper()))
mc(SB + '/sbemu.exe', '::SBEMU/SBEMUORG.EXE')                       # oryginal
mc('/home/claude/ext/sbemu/output/sbemu.exe', '::SBEMU/SBEMU.EXE')   # wersja USB / bez FPU
if os.path.exists('SBTEST.COM'): mc('SBTEST.COM', '::SBTEST.COM')
mc('SBMIX.COM', '::SBMIX.COM')
mc('USBMIX.COM', '::USBMIX.COM')
mc('PAMIEC.COM', '::PAMIEC.COM')
# paczka instalacyjna SBUSB 1.0 Lite (dla innego dysku z DOS-em)
subprocess.check_call(['mmd', '-i', part, '::INSTAL'], env=env)
for f in sorted(os.listdir('/home/claude/release/SBUSB10L')):
    mc('/home/claude/release/SBUSB10L/' + f, '::INSTAL/' + f)
cp_extra = '/home/claude/ext/sbemu/mpxplay/au_cards/sc_usbpcm.c'
mc(cp_extra, '::ZRODLA/SC_USBPC.C')
mc('/home/claude/ext/sbemu_patch.diff', '::ZRODLA/SBEMU.DIF')
mc('/home/claude/ext/sbemu/mpxplay/au_cards/usbpcmct.h', '::ZRODLA/USBPCMCT.H')
mc('files/KOD.TXT', '::ZRODLA/KOD.TXT'); mc('files/CODE.TXT', '::ZRODLA/CODE.TXT')
# MBR
def chs(l):
    c = l // (SPT * HEADS); h = (l // SPT) % HEADS; s = l % SPT + 1
    if c > 1023: c, h, s = 1023, 254, 63
    return bytes([h, s | ((c >> 2) & 0xC0), c & 0xFF])
mbr = bytearray(open('mbr.bin', 'rb').read())
pe = bytes([0x80]) + chs(start) + bytes([0x04]) + chs(total - 1) + struct.pack('<II', start, psize)
mbr[0x1BE:0x1CE] = pe
with open(out, 'wb') as f:
    f.write(mbr); f.write(b'\0' * 512 * (start - 1))
    f.write(open(part, 'rb').read())
    f.truncate(total * 512)
print(out, os.path.getsize(out))
