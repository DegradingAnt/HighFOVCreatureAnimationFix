@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /O2 /MD /W4 /WX /LD CreatureAnimationFix.cpp /Fo:CreatureAnimationFix.obj /link /OUT:CreatureAnimationFix.asi
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /O2 /MD /W4 /WX /DPATCH_TEST CreatureAnimationFix.cpp /Fo:patch_test.obj /Fe:patch_test.exe
exit /b %errorlevel%
