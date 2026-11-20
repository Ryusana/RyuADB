@echo off
chcp 65001 >nul
cd /d "%~dp0"
where py >nul 2>nul && (py -3 ryu_windows_controller.py & goto :done)
where python >nul 2>nul && (python ryu_windows_controller.py & goto :done)
echo Python 3 not found. Install Python 3.10 or newer.
:done
pause
