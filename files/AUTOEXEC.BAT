@ECHO OFF
REM Wspolny dla FreeDOS (CONFIG=1..5) i MS-DOS 6.22/7.1 (CONFIG=GRY, GRYEMS, ...)
REM Shared by FreeDOS (CONFIG=1..5) and MS-DOS 6.22/7.1 (CONFIG=GRY, GRYEMS, ...)
PROMPT $P$G
REM Bufor karty USB = opoznienie dzwieku: L16 (najmniejsze) .. L128. Przy trzaskach zwieksz.
REM USB card buffer = sound latency: L16 (lowest) .. L128. Increase it if you hear crackles.
SET USBBUF=L32
REM Przerwanie emulowanego SB: 5 (standard SB16), 7 albo 9 / emulated SB IRQ: 5, 7 or 9
SET SBIRQ=5
PATH C:\;C:\SBEMU
IF "%CONFIG%"=="1" GOTO GRY
IF "%CONFIG%"=="GRY" GOTO GRY
IF "%CONFIG%"=="2" GOTO GRY
IF "%CONFIG%"=="GRYEMS" GOTO GRY
IF "%CONFIG%"=="3" GOTO TEST
IF "%CONFIG%"=="TEST" GOTO TEST
IF "%CONFIG%"=="5" GOTO DIAG
IF "%CONFIG%"=="DIAG" GOTO DIAG
ECHO.
ECHO  Bez SBEMU / without SBEMU
ECHO  USBTONE        test karty z tonem 1 kHz / card test with a 1 kHz tone
ECHO  USBTONE /LR    ton na zmiane w lewym i prawym kanale / left-right test
ECHO  USBTONE /LOOP  pomiar przez kabel LINE OUT - LINE IN / loopback measurement
ECHO  CPUTEST        test procesora / CPU test
ECHO  PAMIEC         pamiec / memory report
ECHO  POMOC / HELP   opis / help
ECHO.
GOTO END

:DIAG
JLOAD C:\SBEMU\QPIEMU.DLL
LH C:\SBEMU\HDPMI32I -r -x
SET BLASTER=A220 I%SBIRQ% D1 H5 T6
SET USBPCM=V R20 %USBBUF%
ECHO.
ECHO  Diagnostyka: sprawdz klawiature, potem wpisz / Diagnostics: check the keyboard, then type:
ECHO     SBEMU /T6 /H5 /I%SBIRQ%
ECHO  USBPCM=V pokazuje kazdy krok sterownika USB / shows every step of the USB driver
ECHO.
GOTO END

:TEST
SET USBPCM=R20 %USBBUF%
GOTO START
:GRY
SET USBPCM=%USBBUF%
:START
JLOAD C:\SBEMU\QPIEMU.DLL
LH C:\SBEMU\HDPMI32I -r -x
REM PAMIEC /HIGH: SBEMU zostawia swoje bloki na gorze pamieci (wiecej miejsca dla gier)
PAMIEC /HIGH
SBEMU /T6 /H5 /I%SBIRQ%
PAMIEC /LOW
SET BLASTER=A220 I%SBIRQ% D1 H5 T6
IF EXIST C:\USBMIX.CFG USBMIX /L
ECHO.
IF "%CONFIG%"=="3" GOTO TESTHELP
IF "%CONFIG%"=="TEST" GOTO TESTHELP
ECHO  Sound Blaster 16 gotowy / ready: A220 I%SBIRQ% D1 H5 T6
ECHO  GRA / GRACFG   Alien Carnage (GRACFG = ustawienia dzwieku / sound setup)
ECHO  USBMIX         mikser / mixer        SBTEST  test SB
ECHO  PAMIEC         pamiec / memory       POMOC / HELP  opis / help
ECHO.
GOTO END
:TESTHELP
ECHO  Test karty / card test (nagrywanie ostatnich 20 s / recording the last 20 s):
ECHO  SBMIX     test miksowania - sam podaje OK albo BLAD / mixing test, prints OK or ERROR
ECHO  SBTEST    test wszystkich trybow SB / all SB modes
ECHO  USBMIX    mikser, mierniki, odsluch LINE IN / mixer, meters, LINE IN monitor
ECHO  USBREC    mierniki i zapis WAV (USBREC /W) / meters and WAV saving
ECHO  GRA       gra; po wyjsciu nagranie zapisze sie samo / game; recording saved on exit
ECHO.
:END
