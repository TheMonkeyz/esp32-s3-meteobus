# Flash helper for the Guition JC1060P470C, ESP32-P4 (branch port/esp32-p4): the AMOLED's flash_helper.ps1, for the P4.
# Waits for "flash.request" in this folder (p4\), flashes p4\firmware\*.bin, then logs the board's serial output to
# p4\serial_log.txt. "reboot.request": a reset (esptool) and a log, no flashing. "stop.request" or Q / Esc: stop the log.
# The request file may contain the number of seconds to log (default 60).
# Differences from the S3's helper: --chip esp32p4, the bootloader at 0x2000, and the port: with the AMOLED plugged in
# too, the helper finds the P4 once (esptool asks each port's chip; the S3 may restart once) and keeps it in port.txt.
# Delete port.txt if the P4 moves to another COM port. Only ever flashes p4\firmware with ..\tools\esptool.exe.
Set-Location $PSScriptRoot
$Host.UI.RawUI.WindowTitle = "ESP32-P4 flash helper - waiting"
$esptool = Join-Path (Split-Path $PSScriptRoot -Parent) "tools\esptool.exe"

function Stamp { Get-Date -Format "HH:mm:ss" }
function Say($msg, $color = "Gray") {
  Write-Host "[$(Stamp)] $msg" -ForegroundColor $color
  Add-Content -Path "flash_helper.log" -Value "[$(Get-Date -Format s)] $msg" -Encoding ASCII
}
function Status($s) { Set-Content -Path "flash.status" -Value $s -Encoding ASCII }

function Find-P4 {
  if (Test-Path "port.txt") {
    $p = (Get-Content "port.txt" -Raw).Trim()
    if ($p -and ([System.IO.Ports.SerialPort]::GetPortNames() -contains $p)) { return $p }
    Say "port.txt says $p, which isn't connected: looking again" "Yellow"
  }
  $devs = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '\((COM\d+)\)' }
  $cands = @()
  foreach ($d in $devs) {
    $null = $d.Name -match '\((COM\d+)\)'; $c = $Matches[1]
    $rank = if ($d.PNPDeviceID -match 'VID_10C4') { 0 } elseif ($d.PNPDeviceID -match 'VID_303A') { 1 } else { 2 }
    Say "  port $c : $($d.Name) [$($d.PNPDeviceID)]"
    $cands += [pscustomobject]@{ Port = $c; Rank = $rank }
  }
  foreach ($c in ($cands | Sort-Object Rank)) {
    Say "Asking the chip on $($c.Port) ..."
    $out = & $esptool --chip esp32p4 -p $c.Port --before default_reset --after hard_reset chip_id 2>&1 | Out-String
    $out | Add-Content -Path "probe_log.txt" -Encoding ASCII
    if ($LASTEXITCODE -eq 0) {
      Set-Content "port.txt" $c.Port -Encoding ASCII
      Say "ESP32-P4 found on $($c.Port) (saved in port.txt)" "Green"
      return $c.Port
    }
  }
  return $null
}

Say "ESP32-P4 flash helper running in $PSScriptRoot" "Cyan"
foreach ($old in @("flash.request", "reboot.request", "stop.request", "serial.send")) {
  if (Test-Path $old) { Remove-Item $old -Force; Say "Ignored a $old left from before this start" "Yellow" }
}
$port = Find-P4
if ($port) { Status "idle" } else { Say "No ESP32-P4 answered: check the USB cable (the board's UART / USB port) and restart me" "Red"; Status "no_board" }
Say "Waiting for flash.request / reboot.request ..." "Cyan"

while ($true) {
  $req = if (Test-Path "flash.request") { "flash.request" } elseif (Test-Path "reboot.request") { "reboot.request" } else { $null }
  if (-not $req) { Start-Sleep -Seconds 2; continue }
  $reboot = $req -eq "reboot.request"
  $secs = 60
  $txt = (Get-Content $req -Raw -ErrorAction SilentlyContinue)
  if ($txt -match '^\s*(\d{1,4})\s*$') { $secs = [int]$Matches[1] }
  Remove-Item $req -Force
  Remove-Item "flash.done" -Force -ErrorAction SilentlyContinue
  Remove-Item "stop.request" -Force -ErrorAction SilentlyContinue
  $start = Get-Date
  if (-not $port) { $port = Find-P4 }
  if (-not $port) {
    Say "No ESP32-P4 found" "Red"; Status "flash_failed"
    Set-Content "flash.done" "exit=1 stage=port started=$(Get-Date $start -Format s) finished=$(Get-Date -Format s)"
    continue
  }
  Write-Host ""
  Say "=== $(if ($reboot) {'Reboot'} else {'Flash'}) request on $port (serial log: $secs s) ===" "Cyan"
  $Host.UI.RawUI.WindowTitle = "ESP32-P4 flash helper - FLASHING"
  Status "flashing"
  $app = Get-Item "firmware\meteobus.bin" -ErrorAction SilentlyContinue
  if ($app) { Say ("Firmware: {0:N0} bytes, built {1}" -f $app.Length, $app.LastWriteTime.ToString("HH:mm:ss")) }

  $args_ = @("--chip", "esp32p4", "-p", $port, "-b", "460800", "--before", "default_reset", "--after", "hard_reset",
             "write_flash", "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB",
             "0x2000", "firmware\bootloader.bin", "0x8000", "firmware\partition-table.bin",
             "0x10000", "firmware\meteobus.bin")
  if (Test-Path "firmware\ota_data_initial.bin") { $args_ += @("0x610000", "firmware\ota_data_initial.bin") }
  # The Wi-Fi co-processor's (C6) firmware, into the P4's spare app slot (ota_1), for the console's "c6update"
  if (Test-Path "firmware\c6.bin") { $args_ += @("0x310000", "firmware\c6.bin"); Say "Also writing firmware\c6.bin to ota_1 (0x310000)" }
  if ($reboot) { $args_ = @("--chip", "esp32p4", "-p", $port, "--before", "default_reset", "--after", "hard_reset", "chip_id") }
  $lines = New-Object System.Collections.Generic.List[string]
  & $esptool @args_ 2>&1 | ForEach-Object {
    $l = "$_"; $lines.Add($l)
    if ($l -match 'Serial port|Chip is|Writing at .*\(100 %\)|Wrote |Hash of data|Hard resetting|error|Error|failed') { Write-Host "    $l" -ForegroundColor DarkGray }
  }
  $rc = $LASTEXITCODE
  $lines | Set-Content -Path "flash_log.txt" -Encoding ASCII
  $flashSecs = [int]((Get-Date) - $start).TotalSeconds
  if ($rc -ne 0) {
    Say "$(if ($reboot) {'REBOOT'} else {'FLASH'}) FAILED (exit $rc) - last lines:" "Red"
    $lines | Select-Object -Last 6 | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
    Say "Tip: hold BOOT, tap RST, release BOOT, and request again." "Yellow"
    Status "flash_failed"
    Set-Content "flash.done" "exit=$rc stage=flash port=$port started=$(Get-Date $start -Format s) finished=$(Get-Date -Format s)"
    $Host.UI.RawUI.WindowTitle = "ESP32-P4 flash helper - FAILED (waiting)"
    continue
  }
  Say "$(if ($reboot) {'RESET'} else {'FLASH'}) OK on $port in $flashSecs s" "Green"
  Status "logging"
  $Host.UI.RawUI.WindowTitle = "ESP32-P4 flash helper - logging serial ($secs s)"
  Say "Logging serial output for $secs s (serial_log.txt). Q or Esc stops early ..."
  & powershell -NoProfile -ExecutionPolicy Bypass -File "monitor.ps1" -Port $port -Seconds $secs | Out-Null
  $early = if ($LASTEXITCODE -eq 2) { 1 } else { 0 }
  $log = Get-Content "serial_log.txt" -ErrorAction SilentlyContinue
  $errs = @($log | Where-Object { $_ -match '^E \(' }).Count
  $warns = @($log | Where-Object { $_ -match '^W \(' }).Count
  $resets = @($log | Where-Object { $_ -match 'rst:0x' }).Count
  Say ("Serial log saved: {0} lines, {1} errors, {2} warnings, {3} resets" -f $log.Count, $errs, $warns, $resets) "Green"
  $total = [int]((Get-Date) - $start).TotalSeconds
  Set-Content "flash.done" "exit=0 port=$port flash_s=$flashSecs total_s=$total errors=$errs warnings=$warns resets=$resets stopped_early=$early started=$(Get-Date $start -Format s) finished=$(Get-Date -Format s)"
  Status "idle"
  Say "=== Done in $total s. Waiting for the next request ===" "Cyan"
  $Host.UI.RawUI.WindowTitle = "ESP32-P4 flash helper - waiting"
}
