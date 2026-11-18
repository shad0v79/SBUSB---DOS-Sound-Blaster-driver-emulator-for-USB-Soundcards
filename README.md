# SBUSB – Sound Blaster 16 through a USB sound card in DOS

SBUSB is a set of tools and a driver that turns USB sound cards (cheap USB
dongles as well as others like the Behringer UCA202) into a Sound Blaster 16
for FreeDOS and MS-DOS 6.22/7.1. I always felt this was the missing link for
using many cool mini PCs under DOS. Originally created for the eBOX-2300SX,
a mini PC that lacks a sound card.

*Polski opis: plik `files/CZYTAJ.TXT`, licencja: `LICENCJA.TXT`.*

---

## How it works

SBUSB extends [SBEMU](https://github.com/crazii/SBEMU), a Sound Blaster
emulator for pure DOS, with a new output driver for **USB Audio Class 1.0**
cards connected to an **OHCI** USB controller. DOS games see an ordinary
Sound Blaster 16 (`A220 I5 D1 H5 T6`). Their sound goes out through the USB card.

- Isochronous USB streaming, 48 kHz, 16-bit stereo
- Integer arithmetic only, so it runs on CPUs without an FPU (Vortex86SX)
- LINE IN capture: level meters, monitoring and recording to WAV
- Sound Blaster 16 / SB Pro mixer mapped to real volume steps
- Emulated IRQ selectable: 5 (default), 7 or 9
- Adjustable buffer (latency): default about 34 ms, about 20 ms at the lowest setting (measured in QEMU)

## Tested hardware

| Device | Status |
|---|---|
| eBOX-2300SX (DM&P Vortex86SX, no FPU) | works – main target |
| Behringer UCA202 (TI PCM2902) | works – playback and LINE IN |
| GeneralPlus 1B3F:2008 USB dongle | works |
| C-Media CM108 dongles | experimental support, not tested yet |
| MS-DOS 6.22 / 7.1 | installer supports it, not tested on a real MS-DOS yet |

Reports from other mini PCs and USB cards are very welcome.

## Requirements

- 386 or newer CPU, FreeDOS or MS-DOS 6.22/7.1
- an **OHCI** USB controller (UHCI, EHCI and xHCI are not supported)
- a USB Audio Class 1.0 card with a 48 kHz 16-bit output
- the sound card must not share a USB controller with a USB keyboard that the
  BIOS is handling (the driver warns you and tells you which port to use;
  a PS/2 keyboard always works)

## Installation

1. Download `SBUSB-1.0-Lite_EN_installer.zip` (or the Polish `..._PL_...`)
   from **Releases**.
2. Unpack it to any folder on the DOS machine and run `INSTALL`.
3. Reboot and choose a new entry in the boot menu.

The installer copies the files to `C:\SBUSB`, adds boot menu entries to
`FDCONFIG.SYS`/`CONFIG.SYS` and `AUTOEXEC.BAT`, and keeps backups (`*.SBU`).

| Option | Meaning |
|---|---|
| `INSTALL /F` | full menu: games XMS, games XMS+EMS, card test, without SBEMU, diagnostics |
| `INSTALL /2` | short menu: two entries only |
| `INSTALL /U` | uninstall (restore the configuration) |

Details: `files/INSTALL.TXT` and `files/README.TXT`.

## Tools

| Program | What it does |
|---|---|
| `SBEMU.EXE` | Sound Blaster 16 emulator with the USB driver |
| `USBMIX` | text mode mixer with level meters and LINE IN monitor |
| `SBMIX` | self-checking test – plays test signals, analyses LINE IN, prints OK/error; `/L` measures latency |
| `SBTEST` | plays all Sound Blaster modes |
| `USBREC` | level meters; saves the last seconds of output and LINE IN as WAV |
| `USBTONE` | tests the USB card without SBEMU; `/I` writes the card's descriptors to a log |
| `CPUTEST`, `PAMIEC` | CPU info, free memory |

A loopback cable (LINE OUT → LINE IN) lets the test programs check what the card
really outputs.

## Known limits

- FM (OPL3) music: SBEMU's OPL emulation needs an FPU, so it is not available on
  the Vortex86SX.
- One USB audio device, OHCI controllers only, 48 kHz output only.
- Some messages of the test tools and the USB driver are in Polish.

## Building from source

- DOS tools: GCC with `-m16` (real-mode `.COM` files), NASM – see `build.sh`
- SBEMU: DJGPP cross compiler (GCC 12.2); full modified source in `SBEMUSRC.ZIP`,
  changes only in `SBEMU.DIF`
- CF card image: `build_img.py` (mtools, dosfstools)
- Code description: `files/CODE.TXT` (EN), `files/KOD.TXT` (PL)

## Licence

Created by **Lukasz Bolesta, with the help of Claude** (an AI assistant by Anthropic).

- SBUSB tools, scripts and docs: **MIT**
- `SBEMU.EXE` and the USB driver inside it: **GNU GPL v2** (`GPL2.TXT`)
- Bundled third-party programs (Jemm, HimemX, HDPMI) keep their own licences

See `LICENSE.TXT` (English) or `LICENCJA.TXT` (Polish) for full details.

## Thanks

crazii and the SBEMU contributors, Attila Padar (MPXPlay), the DOSBox Team,
Japheth (Jemm, HimemX, HX), Tom Ehlert, the FreeDOS project, DJ Delorie (DJGPP),
and the authors of GCC, NASM, QEMU, mtools and Rufus.

*Sound Blaster is a trademark of Creative Technology Ltd. This project is not
affiliated with Creative or any other company named here.*
