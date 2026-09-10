<#
.SYNOPSIS
Standalone Windows flasher for a BLE gas pedal firmware package.

.DESCRIPTION
Flashes the images listed in the flash_args file that sits next to this
script. No ESP-IDF installation is required: if no usable esptool is found,
the pinned standalone esptool release is downloaded from GitHub, its sha256
verified with Get-FileHash, and it is cached in .esptool\ next to this script.

By default this does a full chip erase before flashing, so the unit always
comes up with no stale configuration - the safest, most reproducible result.
A full erase also wipes the NVS partition, which holds the BLE bond keys, so
the throttle and motor units must be re-paired afterwards. To re-flash a bench
unit without erasing, pass -NoErase.

esptool resolution order:
  1. esptool.exe on PATH      2. esptool.py on PATH
  3. py -m esptool / python -m esptool / python3 -m esptool
  4. cached .esptool\esptool.exe from a previous download
  5. download the pinned release (sha256-verified)

Driver note: an ESP32-S3 connected via its native USB port is driverless on
Windows 10+. Boards that route the serial console through a UART bridge may
need the CP210x or CH340 driver installed before a COM port appears.

.PARAMETER Port
Serial port, e.g. COM5. If omitted, the connected COM ports are enumerated
and exactly one match is required - flash the two units one at a time.

.PARAMETER Baud
Flash and monitor baud rate. Defaults to 115200, the ESP console default
(native USB ignores baud). When flashing fast with -Baud 460800 on a
UART-bridge board, watch the console with a separate flash.ps1 -MonitorOnly
afterwards so the monitor stays at 115200.

.PARAMETER NoErase
Skip the full chip erase and keep the existing flash contents, including the
BLE bond keys in NVS. Use when re-flashing a unit that is already paired.

.PARAMETER EraseFlash
Accepted for back-compat; a full erase is now the default, so this switch is a
no-op.

.PARAMETER Monitor
Attach a serial monitor to the device console after flashing (Ctrl+C exits).
Opening the port may reset UART-bridge boards once - you will see the boot
banner, which is useful.

.PARAMETER MonitorOnly
Only attach the serial monitor - no flashing, no esptool needed or downloaded.

.PARAMETER Clean
Delete the downloaded .esptool\ cache and exit.

.EXAMPLE
.\flash.ps1

.EXAMPLE
.\flash.ps1 -Port COM5 -Baud 460800

.EXAMPLE
.\flash.ps1 -MonitorOnly
#>
[CmdletBinding()]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [switch]$NoErase,
    [switch]$EraseFlash,
    [switch]$Monitor,
    [switch]$MonitorOnly,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Pinned standalone esptool release used when nothing suitable is installed.
# v4.x matches the underscore-style flags that ESP-IDF v5.5.2 writes into
# flasher_args.json. sha256 is the digest published for the official
# espressif/esptool GitHub release asset.
$EsptoolVersion = "v4.11.0"
$EsptoolAsset = "esptool-$EsptoolVersion-windows-amd64.zip"
$EsptoolUrl = "https://github.com/espressif/esptool/releases/download/$EsptoolVersion/$EsptoolAsset"
$EsptoolSha256 = "0edab659cca62df69c91a80cc685c1c3bbdefb7eff53abf43c2b902a735ad2f9"
$EsptoolCacheDir = Join-Path $ScriptDir ".esptool"

if ($Clean) {
    if (Test-Path $EsptoolCacheDir) {
        Remove-Item -Recurse -Force $EsptoolCacheDir
        Write-Host "Removed $EsptoolCacheDir"
    } else {
        Write-Host "Nothing to clean ($EsptoolCacheDir does not exist)."
    }
    exit 0
}

$FlashArgsFile = Join-Path $ScriptDir "flash_args"

# Package metadata (chip, unit name, version) written by package.py. Keeping it
# in a data file rather than templating this script means flash.ps1 is the exact
# same reviewed file in the repo and in every package.
$Meta = @{
    UNIT       = "unit"
    UNIT_LABEL = "unit"
    CHIP       = "esp32s3"
    VERSION    = "unknown"
    COMMIT     = "unknown"
    BEFORE     = "default_reset"
    AFTER      = "hard_reset"
}
$MetaFile = Join-Path $ScriptDir "package.env"
if (Test-Path $MetaFile) {
    foreach ($line in Get-Content $MetaFile) {
        if ($line -match '^\s*([A-Z_]+)="(.*)"\s*$') { $Meta[$Matches[1]] = $Matches[2] }
    }
}

# Resolve the serial port when none was given explicitly.
function Resolve-SerialPort {
    param([string]$Requested)
    if ($Requested) { return $Requested }
    $ports = @()
    try {
        $ports = @([System.IO.Ports.SerialPort]::GetPortNames())
    } catch {
        # SerialPort is not available on every PowerShell edition - fall back
        # to the registry map of active COM ports.
        $reg = Get-ItemProperty "HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM" -ErrorAction SilentlyContinue
        if ($reg) {
            $ports = @($reg.PSObject.Properties |
                Where-Object { $_.Name -notlike "PS*" } |
                ForEach-Object { $_.Value })
        }
    }
    if ($ports.Count -eq 0) {
        Write-Error "No COM port found. Plug in the $($Meta.UNIT_LABEL) (native USB is driverless on Win10+; UART-bridge boards may need the CP210x/CH340 driver), or pass -Port COMx."
    }
    if ($ports.Count -gt 1) {
        Write-Error "Multiple COM ports found: $($ports -join ', '). Both units are probably connected - flash them one at a time, or disambiguate with -Port COMx."
    }
    return $ports[0]
}

# Serial monitor: SerialPort with DTR asserted (native USB-CDC only emits
# data with DTR set) and RTS deasserted (avoids holding EN low on UART-bridge
# auto-reset circuits). Ctrl+C exits; the finally block closes the port.
function Start-SerialMonitor {
    param([string]$MonPort, [int]$MonBaud)
    $sp = New-Object System.IO.Ports.SerialPort $MonPort, $MonBaud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.DtrEnable = $true
    $sp.RtsEnable = $false
    $sp.ReadTimeout = 200
    try {
        $sp.Open()
        Write-Host "--- Monitor attached: $MonPort, baud $MonBaud. Press Ctrl+C to exit. ---"
        Write-Host "--- Opening the port may reset UART-bridge boards once (boot banner follows). ---"
        while ($sp.IsOpen) {
            $data = $sp.ReadExisting()
            if ($data) { Write-Host -NoNewline $data }
            Start-Sleep -Milliseconds 50
        }
        Write-Host "Monitor ended (port closed)."
    } finally {
        if ($sp.IsOpen) { $sp.Close() }
        $sp.Dispose()
        Write-Host ""
        Write-Host "Monitor stopped."
    }
}

if ($MonitorOnly) {
    $Port = Resolve-SerialPort -Requested $Port
    Start-SerialMonitor -MonPort $Port -MonBaud $Baud
    exit 0
}

if (-not (Test-Path $FlashArgsFile)) {
    Write-Error "flash_args not found next to this script. Run flash.ps1 from inside the unzipped package."
}

# Try a candidate esptool invocation by actually running "version".
function Test-EsptoolCandidate {
    param([string[]]$Cmd)
    $prev = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $extra = @($Cmd | Select-Object -Skip 1) + @("version")
        $null = & $Cmd[0] $extra 2>$null
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Get-EsptoolDownload {
    Write-Host "No esptool found - downloading standalone esptool $EsptoolVersion (windows-amd64)..."
    Write-Host "  $EsptoolUrl"
    # PowerShell 5.1 defaults to TLS 1.0 which GitHub rejects.
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $zipPath = Join-Path $ScriptDir ".esptool.download.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $EsptoolUrl -OutFile $zipPath

    $actual = (Get-FileHash -Algorithm SHA256 -Path $zipPath).Hash.ToLowerInvariant()
    if ($actual -ne $EsptoolSha256) {
        Remove-Item -Force $zipPath
        Write-Error "sha256 mismatch for $EsptoolAsset - expected $EsptoolSha256, got $actual"
    }

    if (Test-Path $EsptoolCacheDir) { Remove-Item -Recurse -Force $EsptoolCacheDir }
    $extractDir = Join-Path $ScriptDir ".esptool.extract"
    if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
    Expand-Archive -Path $zipPath -DestinationPath $extractDir
    # The zip contains a single esptool-windows-amd64\ directory - flatten it.
    $inner = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
    Move-Item -Path $inner.FullName -Destination $EsptoolCacheDir
    Remove-Item -Recurse -Force $extractDir
    Remove-Item -Force $zipPath
    Write-Host "esptool cached in $EsptoolCacheDir (remove with -Clean)"
}

# Resolve esptool as a command array (first element = executable).
$cachedExe = Join-Path $EsptoolCacheDir "esptool.exe"
$Esptool = $null
$candidates = @(
    ,@("esptool.exe")
    ,@("esptool.py")
    ,@("py", "-m", "esptool")
    ,@("python", "-m", "esptool")
    ,@("python3", "-m", "esptool")
    ,@($cachedExe)
)
foreach ($cand in $candidates) {
    if (($cand[0] -eq $cachedExe) -and (-not (Test-Path $cachedExe))) { continue }
    if (Test-EsptoolCandidate -Cmd $cand) { $Esptool = $cand; break }
}
if (-not $Esptool) {
    Get-EsptoolDownload
    if (-not (Test-EsptoolCandidate -Cmd @($cachedExe))) {
        Write-Error "Downloaded esptool does not run on this system."
    }
    $Esptool = @($cachedExe)
}
Write-Host "Using esptool: $($Esptool -join ' ')"

$Port = Resolve-SerialPort -Requested $Port

# Parse flash_args into explicit arguments. Not passed as "@flash_args"
# because "@" is PowerShell's splatting operator; explicit parsing is also
# more robust across esptool versions.
$flashArgs = @()
foreach ($line in Get-Content $FlashArgsFile) {
    $trimmed = $line.Trim()
    if ($trimmed) { $flashArgs += ($trimmed -split "\s+") }
}

$common = @("--chip", $Meta.CHIP, "--port", $Port, "--baud", "$Baud",
            "--before", $Meta.BEFORE, "--after", $Meta.AFTER)
$exe = $Esptool[0]
$exeArgs = @($Esptool | Select-Object -Skip 1)

Write-Host "=== $($Meta.UNIT_LABEL) firmware $($Meta.VERSION) (commit $($Meta.COMMIT)) ==="

Push-Location $ScriptDir
try {
    if (-not $NoErase) {
        Write-Host "Erasing entire flash on $Port (default full clean slate; this wipes the"
        Write-Host "NVS partition, so the throttle and motor units must be re-paired"
        Write-Host "afterwards - pass -NoErase to keep the existing bond keys)..."
        & $exe ($exeArgs + $common + @("erase_flash"))
        if ($LASTEXITCODE -ne 0) { Write-Error "esptool erase_flash failed (exit $LASTEXITCODE)" }
    }
    else {
        Write-Host "Keeping existing flash contents (-NoErase): NVS and BLE bond keys are"
        Write-Host "preserved."
    }

    Write-Host "Flashing $($Meta.UNIT_LABEL) ($Port, baud $Baud)..."
    & $exe ($exeArgs + $common + @("write_flash") + $flashArgs)
    if ($LASTEXITCODE -ne 0) { Write-Error "esptool write_flash failed (exit $LASTEXITCODE)" }
    Write-Host "Done. The $($Meta.UNIT_LABEL) reboots into the new firmware."
} finally {
    Pop-Location
}

if ($Monitor) {
    Start-SerialMonitor -MonPort $Port -MonBaud $Baud
}
