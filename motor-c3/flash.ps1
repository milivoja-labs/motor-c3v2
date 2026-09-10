#Requires -Version 5.1
<#
.SYNOPSIS
    Build / flash / monitor the motor-side ESP32-C3 firmware from Windows.

.DESCRIPTION
    Thin wrapper around `idf.py`. Run it from an ESP-IDF PowerShell (the one that
    has run export.ps1 so idf.py is on PATH). It defaults to the BENCH-SAFE build
    (MOTOR_OUTPUT_ENABLED=0) — no physical motor output.

    Actions:
      build    - configure + build (bench-safe unless -EnableOutput)
      flash    - build + flash + open monitor
      monitor  - open the serial monitor only (no build, no flash)
      erase    - erase the whole flash (clears app + BLE bonds), then exit

.PARAMETER Action
    build | flash | monitor | erase   (default: flash)

.PARAMETER Port
    Serial port, e.g. COM5. Auto-detected if exactly one COM port is present.

.PARAMETER Baud
    Flash baud (default 460800). Monitor is always 115200.

.PARAMETER EnableOutput
    DANGER: build with MOTOR_OUTPUT_ENABLED=1 (physical PPM output). Requires
    technician sign-off. The script prints a confirmation prompt first.

.EXAMPLE
    .\flash.ps1                       # bench-safe build+flash+monitor, auto port
.EXAMPLE
    .\flash.ps1 -Action monitor -Port COM5
.EXAMPLE
    .\flash.ps1 -Action flash -Port COM5 -EnableOutput   # live output (prompts)
.EXAMPLE
    .\flash.ps1 -Action erase -Port COM5                 # wipe flash + bonds

.NOTES
    Needs ESP-IDF v5.x on PATH (idf.py). Target is esp32c3.
#>

[CmdletBinding()]
param(
    [ValidateSet('build', 'flash', 'monitor', 'erase')]
    [string]$Action = 'flash',

    [string]$Port,
    [int]$Baud = 460800,
    [switch]$EnableOutput
)

$ErrorActionPreference = 'Stop'
$Chip = 'esp32c3'

function Assert-Idf {
    if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        throw "idf.py not found on PATH. Open an ESP-IDF PowerShell (run the IDF export.ps1) and retry."
    }
}

function Get-ComPorts {
    try { return [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object }
    catch {
        return (Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty DeviceID | Sort-Object)
    }
}

function Resolve-Port {
    param([string]$Given)
    if ($Given) { return $Given }
    $ports = Get-ComPorts
    if (-not $ports -or $ports.Count -eq 0) {
        throw "No COM ports found. Plug in the C3 board, or pass -Port COMx."
    }
    if ($ports.Count -eq 1) {
        Write-Host "Auto-selected $($ports[0])." -ForegroundColor Yellow
        return $ports[0]
    }
    throw "Multiple COM ports ($($ports -join ', ')). Pass -Port COMx to choose."
}

function Invoke-Idf {
    param([string[]]$IdfArgs)
    Write-Host "  > idf.py $($IdfArgs -join ' ')" -ForegroundColor DarkGray
    & idf.py @IdfArgs
    if ($LASTEXITCODE -ne 0) { throw "idf.py exited with code $LASTEXITCODE" }
}

# --- output-enable guard --------------------------------------------------
$OutputFlag = 0
if ($EnableOutput) {
    Write-Host ""
    Write-Host "############################################################" -ForegroundColor Red
    Write-Host "#  MOTOR_OUTPUT_ENABLED=1 — PHYSICAL PPM OUTPUT             #" -ForegroundColor Red
    Write-Host "#  Wheel off the ground. VESC neutral + current limits set. #" -ForegroundColor Red
    Write-Host "#  This should only be done after technician sign-off.      #" -ForegroundColor Red
    Write-Host "############################################################" -ForegroundColor Red
    $ans = Read-Host "Type ENABLE to confirm physical output, anything else to abort"
    if ($ans -ne 'ENABLE') { Write-Host "Aborted; staying bench-safe."; exit 1 }
    $OutputFlag = 1
}

Assert-Idf
Write-Host "motor-c3 loader  action=$Action  chip=$Chip  output=$OutputFlag" -ForegroundColor Magenta

# Ensure the target is set (no-op if already esp32c3).
Invoke-Idf -IdfArgs @('set-target', $Chip)

switch ($Action) {
    'build' {
        Invoke-Idf -IdfArgs @("-DMOTOR_OUTPUT_ENABLED=$OutputFlag", 'build')
    }
    'flash' {
        $p = Resolve-Port -Given $Port
        Invoke-Idf -IdfArgs @("-DMOTOR_OUTPUT_ENABLED=$OutputFlag", '-p', $p, '-b', "$Baud", 'flash', 'monitor')
    }
    'monitor' {
        $p = Resolve-Port -Given $Port
        Write-Host "Monitor on $p (Ctrl+] to quit)." -ForegroundColor Cyan
        Invoke-Idf -IdfArgs @('-p', $p, 'monitor')
    }
    'erase' {
        $p = Resolve-Port -Given $Port
        Write-Host "Erasing flash on $p (clears app AND BLE bonds)..." -ForegroundColor Yellow
        Invoke-Idf -IdfArgs @('-p', $p, 'erase-flash')
        Write-Host "Erase complete." -ForegroundColor Green
    }
}
