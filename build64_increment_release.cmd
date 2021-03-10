@echo off

cd /d %~dp0
for /f "tokens=3" %%i in ('findstr /B /R /C:"VBOX_VERSION_MAJOR *=" Version.kmk') do SET VBOX_VER_MJ=%%i
for /f "tokens=3" %%i in ('findstr /B /R /C:"VBOX_VERSION_MINOR *=" Version.kmk') do SET VBOX_VER_MN=%%i
for /f "tokens=3" %%i in ('findstr /B /R /C:"VBOX_VERSION_BUILD *=" Version.kmk') do SET VBOX_VER_BLD=%%i
for /f "tokens=6" %%i in ('findstr /C:"$Rev: " Config.kmk') do SET VBOX_REV=%%i
for /f "tokens=3" %%i in ('findstr /B /C:"VBOX_BUILD_PUBLISHER :=" LocalConfig.kmk') do SET VBOX_VER_PUB=%%i

set VERSION=%VBOX_VER_MJ%.%VBOX_VER_MN%.%VBOX_VER_BLD%%VBOX_VER_PUB%-r%VBOX_REV%
set VBOX_VER_MJ=
set VBOX_VER_MN=
set VBOX_VER_BLD=
set VBOX_VER_PUB=

echo "INSTALLER CONFIGURATION"

call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.Cmd" /Release /x64 /win7
if ERRORLEVEL 1 exit /b 1

set BUILD_TARGET_ARCH=amd64
set PATH=%PATH%;%~dp0kBuild\bin\win.amd64

cscript configure.vbs --with-DDK=C:\WinDDK\7600.16385.1 --with-MinGW-w64=C:\lib\mingw\mingw64 --with-MinGW32=C:\lib\mingw\mingw32 --with-libSDL=C:\lib\SDL\x64\SDL-1.2.15 --with-openssl=C:\lib\OpenSSL\x64 --with-openssl32=C:\lib\OpenSSL\x86 --with-libcurl=C:\lib\curl\x64 --with-libcurl32=C:\lib\curl\x86 --with-Qt5=C:\Qt\5.6.3\msvc2010_64 --with-libvpx=C:\lib\libvpx --with-libopus=C:\lib\libopus --with-python=C:/Python27
if ERRORLEVEL 1 exit /b 1

call env.bat
if ERRORLEVEL 1 exit /b 1

REM Commands for verbose output
REM kmk --pretty-command-printing --jobs=1
REM kmk --debug=vjm KBUILD_TYPE=debug
kmk
if ERRORLEVEL 1 exit /b 1

cd .\out\win.amd64\release\bin\

call comregister.cmd
if ERRORLEVEL 1 exit /b 1

call loadall.cmd
if ERRORLEVEL 1 exit /b 1

del /q AutoConfig.kmk configure.log env.bat 2>nul

start VirtualBox.exe
