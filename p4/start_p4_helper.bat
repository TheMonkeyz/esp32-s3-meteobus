@echo off
cd /d "%~dp0"
rem The Guition JC1060P470C (ESP32-P4) flash helper (branch port/esp32-p4). Close its window to stop it.
start "ESP32-P4 flash helper" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_helper_p4.ps1"
