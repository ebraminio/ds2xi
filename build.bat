@echo off
setlocal

if not defined VSCMD_VER call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cl.exe -nologo -LD -W4 -Ox -GS- -std:c++20 -Ihidapi\hidapi -DHID_API_NO_EXPORT_DEFINE ^
  -Fo".\\" -Fe".\XInput1_4.dll" main.cpp hidapi\windows\hid.c ^
  -link /ENTRY:DllMain /NODEFAULTLIB /SUBSYSTEM:WINDOWS ^
  shlwapi.lib kernel32.lib user32.lib ^
  /def:xinput.def /implib:".\XInput1_4.lib" || exit /b 1

endlocal
