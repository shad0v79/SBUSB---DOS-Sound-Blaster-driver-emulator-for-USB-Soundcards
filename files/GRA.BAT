@ECHO OFF
CD \CARNAGE
CARNAGE %1 %2 %3
CD \
IF "%CONFIG%"=="3" GOTO ZAPIS
IF "%CONFIG%"=="TEST" GOTO ZAPIS
GOTO KONIEC
:ZAPIS
ECHO.
ECHO Zapisuje nagranie z gry / saving the recording...
\USBREC /W
:KONIEC
