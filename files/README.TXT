SOUND BLASTER 16 OVER USB FOR THE eBOX-2300SX - USER GUIDE
===========================================================
(Wersja polska: CZYTAJ.TXT)

This image boots DOS (FreeDOS 1.4) with SBEMU - a Sound Blaster 16
emulator - extended with a USB sound card driver. DOS games see an
ordinary SB16 (A220 I5 D1 H5 T6); the sound goes out through the USB card.

Tested on: eBOX-2300SX (DM&P Vortex86SX, no FPU), Behringer UCA202
(TI PCM2902 chip). Experimental: cards with the C-Media CM108 chip.


1. WRITING THE IMAGE
--------------------
Write ebox_cf16.img to a CF card (16 MB or larger) or a USB stick with
Rufus ("DD image" mode) or Win32DiskImager. Set the eBOX BIOS to boot
from it.


2. CONNECTIONS (IMPORTANT)
--------------------------
* USB keyboard: front LEFT socket.
* USB sound card: front RIGHT or rear socket.
  The BIOS drives the USB keyboard with the interrupts of the first USB
  controller (front left socket). The front right and rear sockets share
  the second controller; the card and a USB keyboard cannot both be on it
  - SBEMU then does not find the card and tells you what to move.
* A PS/2 keyboard always works.
* A 2x RCA cable LINE OUT -> LINE IN (optional) lets the test programs
  check what really comes out of the card. Unplug it only when you turn
  on the LINE IN monitor (feedback loop).


3. BOOT MENU
------------
  1 - Games: SB16, XMS memory            <- default
  2 - Games: SB16, XMS + EMS memory      (games that need EMS)
  3 - Card test, recording, mixer        (SBMIX, USBREC, USBMIX)
  4 - Without SBEMU: USBTONE, CPUTEST    (tests of the USB card alone)
  5 - Diagnostics: you start SBEMU yourself, with messages

Options 1, 2 and 3 load: JEMMEX (XMS, UMB, optionally EMS), QPIEMU
(real-mode games), HDPMI32i (DPMI) and SBEMU, set BLASTER and load the
saved mixer settings (USBMIX.CFG).


4. MEMORY: XMS, EMS, UMB, DPMI
------------------------------
                       option 1     option 2         option 4
  XMS                  yes          yes              yes (HIMEMX)
  EMS                  no           yes (frame D000) no
  UMB (upper memory)   about 96 KB  about 32 KB      no
  DPMI / VCPI          yes / yes    yes / yes        no / no
  free for games       about 613 KB* similar         about 618 KB*
  * measured in the QEMU emulator; check on the eBOX with PAMIEC.

* Most games: option 1.
* A game says EMS (Expanded Memory) is missing - option 2. The EMS page
  frame is at D000 because E000-EFFF is used by the eBOX BIOS and the
  USB controllers.
* Games with DOS4GW and other extenders use DPMI (HDPMI32i) or VCPI
  (JEMMEX) - they work in options 1 and 2.
* Old games that get confused by a lot of XMS: JEMMEX reports at most
  8 MB to them (X2MAX=8192); newer programs (XMS 3.0) see all of it.
* Games that refuse to run under a memory manager (they need "plain"
  real mode) cannot have the emulated SB - SBEMU needs JEMMEX to trap
  the card's ports. They run in option 4 (without SB sound).
* SBEMU leaves its blocks at the top of conventional memory (PAMIEC
  /HIGH in AUTOEXEC) and HDPMI32i loads into a UMB - hence about 613 KB.


5. PLAYING
----------
  GRACFG  - Alien Carnage sound setup (choose Sound Blaster, Music)
  GRA     - Alien Carnage
SB settings for other games: port 220, IRQ 5, DMA 1, DMA16 5, SB16.
Change the IRQ in AUTOEXEC.BAT: SET SBIRQ=5 (5, 7 or 9 possible) - then
set it in the game too (e.g. GRACFG).
Limitations:
* No FM music (OPL/AdLib): the Vortex86SX has no FPU and the FM emulation
  needs one. Games with FM music play only their digital sound. Games
  with digital music (MOD, e.g. Alien Carnage) play completely.
* No MIDI (MPU-401).
* Without FM, digital sound plays at full scale (the original SBEMU
  halved it to leave room for FM).


6. PROGRAMS
-----------
USBMIX   Mixer with sliders: master and digital (voice) volume (SB16
         registers, 2 dB per step, left/right separately), LINE IN monitor
         (-46..0 dB), output and LINE IN meters.
           arrows - select and change   PgUp/PgDn - 10 dB
           Tab - both/left/right   M - mute   S - save   Esc - quit
         USBMIX /L loads the saved settings (AUTOEXEC does it).
         Games often set the mixer themselves - their settings win then.
         If LINE IN clips (feedback), the monitor turns itself off.
SBMIX    Card test "like a game": a 440 Hz tone (music) and 2 kHz beeps
         (effects), written to the DMA buffer in 4 ways. In option 3 it
         checks the recording BY ITSELF and prints OK or BLAD (= error),
         also for LINE IN if the cable is connected. Result in SBMIX.LOG.
         SBMIX /L measures latency: from the start of a sound on the SB to
         sending it over USB (and back on LINE IN). Mostly the driver buffer.
SBTEST   Tests all SB modes (8/16 bit, mono/stereo, IRQ). In test 6 the
         lower tone must be on the LEFT. SBTEST /N - no questions.
         Log: SBTEST.LOG.
USBREC   Recording (option 3): the driver keeps the last 20 s of sound
         (output and LINE IN; digital silence longer than 1 s does not
         overwrite the recording).
           USBREC        meters (Esc - quit)
           USBREC /I     status         USBREC /W   save RECOUT.WAV,
           USBREC /C     clear                      RECIN.WAV, STAN.TXT
           USBREC /W /S:10  only the last 10 s
         In option 3 GRA saves the recording itself when the game ends.
         Old files get a number (RECOUT01.WAV...), nothing is overwritten.
         A full save takes about 7.5 MB - one fits on a 16 MB card.
USBTONE  Tests the USB card without SBEMU (option 4 only): 1 kHz tone.
           /LR    left and right channel in turn (shows which one plays)
           /LOOP  measurement through a LINE OUT -> LINE IN cable (LOOP.WAV)
           /S     PCI and port scan only (changes nothing, no card details)
           /I     card description (descriptors) to the log, no sound - for
                  new cards
         Result in USBTONE.LOG (old ones are numbered: USBTON01.LOG...).
CPUTEST  CPU test.
PAMIEC   Memory: conventional (block list), UMB, XMS, EMS, VCPI, DPMI.
         PAMIEC /E in English, PAMIEC /K without the block list.
HELP     This guide (POMOC - Polish). STRONA file.txt - any text file.
The test programs print Polish messages (no Polish letters).


7. USB DRIVER OPTIONS (USBPCM variable, set BEFORE SBEMU)
--------------------------------------------------------
Example: SET USBPCM=L64 R20
  Cn   use OHCI controller number n (0 = first)
  Ln   buffer of n ms (16-128). AUTOEXEC sets it with the USBBUF
       variable (L32); buffer = sound latency. If you hear crackles raise
       it (L48, L64); for the lowest latency try L16 and check with SBMIX.
  Rn   keep recording the last n seconds (1-60) of output and LINE IN
  I/O  with R: record only LINE IN (I) or only the output (O)
  X    do not start the LINE IN stream (no meters, monitor, recording)
  W    swap left and right (as the original SBEMU did)
  V    all messages on screen
Driver log: USBPCM.LOG. The original SBEMU: \SBEMU\SBEMUORG.EXE.


8. MS-DOS 6.22 / 7.1 INSTEAD OF FreeDOS (UNTESTED)
-------------------------------------------------
MS-DOS cannot be included (licence), but the image is prepared: the root
directory has CONFIG.SYS (menu in MS-DOS syntax, the same 5 options), a
shared AUTOEXEC.BAT and reserved first directory entries for IO.SYS and
MSDOS.SYS.
  1. Write the image to the CF card as usual.
  2. Boot the eBOX from an MS-DOS 6.22 or 7.1 boot floppy/stick (the CF
     card must then be drive C:).
  3. Type:  SYS C:
     (replaces FreeDOS with MS-DOS: boot sector, IO.SYS, MSDOS.SYS,
     COMMAND.COM; all other files stay).
  4. Reboot from the CF card - the MS-DOS menu appears.
If SYS refuses ("No room for system"): format the card with MS-DOS
(FORMAT C: /S), open ebox_cf16.img in 7-Zip on a PC and copy all files
(except KERNEL.SYS, COMMAND.COM, IO.SYS, MSDOS.SYS) to the card.
Back to FreeDOS: write the image again.
MS-DOS 7.1 (from Windows 98): if after SYS it tries to start Windows
instead of the menu, set BootGUI=0 in the [Options] section of
C:\MSDOS.SYS (a hidden text file).


8a. INSTALLING ON ANOTHER DISK WITH DOS (SBUSB 1.0 Lite)
--------------------------------------------------------
The \INSTAL\EN and \INSTAL\PL directories hold an installer package for an
existing FreeDOS or MS-DOS 6.22/7.1: INSTALL adds boot menu entries with
SBEMU and the USB card. Description: \INSTAL\EN\INSTALL.TXT.


9. C-MEDIA CM108 CARDS (EXPERIMENTAL)
-------------------------------------
The driver supports USB Audio 1.0 cards with a 48 kHz 16-bit stereo
output. For the CM108 it adds: mono input (microphone) copied to both
channels, and unmuting the Feature Units at start. Cards with an
internal microphone monitor get it muted. The GeneralPlus 1B3F:2008 card
works; a real CM108 has not been tested. First step with a new card:
option 4, USBTONE /I (unplug and plug the card when asked), then send
USBTONE.LOG. USBTONE /LOOP needs a stereo input, so it will not work
with a mono card - use SBMIX in option 3 instead.


10. TROUBLESHOOTING
-------------------
* "No supported soundcard found": the card is in the wrong socket (see 2)
  or not connected. Details: USBPCM.LOG.
* The USB keyboard stops working after SBEMU starts: the card is in the
  front LEFT socket - move it (see 2).
* Left and right swapped in some game: SET USBPCM=W before SBEMU (option 5).
* The left column of characters is cut off: "Auto" in the LCD menu.
* Files to send when something fails: USBPCM.LOG, SBMIX.LOG, SBTEST.LOG,
  USBTONE.LOG, the output of PAMIEC > PAMIEC.TXT.


11. CONTENTS AND LICENCES
-------------------------
Creator: Lukasz Bolesta, with the help of Claude (AI assistant by Anthropic).
Full details: LICENSE.TXT (EN), LICENCJA.TXT (PL), GPL text: GPL2.TXT.

  KERNEL.SYS, COMMAND.COM  FreeDOS 1.4 (GNU GPL), https://github.com/FDOS
  HIMEMX.EXE               XMS manager (Japheth; Public Domain +
                           FreeDOS Himem, GPL/Artistic)
  SBEMU\SBEMU.EXE          SBEMU (GNU GPL v2) with the USB module and
                           changes; full source: ZRODLA\SBEMUSRC.ZIP,
                           changes: ZRODLA\SBEMU.DIF
  SBEMU\JEMMEX, JLOAD, QPIEMU  Jemm (Japheth and others, Artistic License)
  SBEMU\HDPMI32I.EXE       HX DOS Extender (Japheth, crazii), freeware
  *.COM, INSTAL\           USBTONE, CPUTEST, SBTEST, SBMIX, USBREC, USBMIX,
                           PAMIEC, STRONA, INSTALL - MIT licence
  CARNAGE\                 Alien Carnage, freeware (c) John Passfield,
                           unmodified files of the 2007 release - for
                           private use; do not distribute the image with
                           the game.
  ZRODLA\                  sources; code description: CODE.TXT (EN),
                           KOD.TXT (PL)
