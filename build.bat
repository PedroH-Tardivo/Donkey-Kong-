@echo off
rem Compila a ROM no Windows usando o SGDK.
rem Requer a variavel de ambiente GDK apontando para a pasta do SGDK
rem (ex.: set GDK=C:\sgdk).

if "%GDK%"=="" (
    echo Defina a variavel GDK com a pasta do SGDK. Exemplo:
    echo     set GDK=C:\sgdk
    exit /b 1
)

"%GDK%\bin\make" -f "%GDK%\makefile.gen" %*
if errorlevel 1 exit /b 1

if not exist rom mkdir rom
copy /Y out\rom.bin rom\DonkeyKong.bin >nul
echo.
echo ROM gerada: rom\DonkeyKong.bin
