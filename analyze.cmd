@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /MD /W4 /WX /analyze /c CreatureAnimationFix.cpp /Fo:analyzed.obj
exit /b %errorlevel%
