# Arduino Libraries Setup Script
# Links all repo-managed libraries into Arduino's libraries folder via symlinks.
# Run as Administrator.
#
# Managed libraries (all in libraries/ as git submodules or source):
#   - LightEngine       (local source)
#   - ESP32-BLE-MIDI    (submodule: DigitallyRemastered/ESP32-BLE-MIDI)
#   - NimBLE-Arduino    (submodule: DigitallyRemastered/NimBLE-Arduino)

Write-Host "Arduino Libraries Setup" -ForegroundColor Cyan
Write-Host "=======================" -ForegroundColor Cyan
Write-Host ""

$arduinoLibs = "$env:USERPROFILE\Documents\Arduino\libraries"

# Check if Arduino libraries folder exists
if (!(Test-Path $arduinoLibs)) {
    Write-Host "ERROR: Arduino libraries folder not found at:" -ForegroundColor Red
    Write-Host "  $arduinoLibs" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install Arduino IDE first." -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

# ---- Library definitions ----------------------------------------------------
# Each entry: @{ Name = "ArduinoFolderName"; Src = "relative\path\in\repo" }
$libraries = @(
    @{ Name = "LightEngine";    Src = "libraries\LightEngine" },
    @{ Name = "ESP32-BLE-MIDI"; Src = "libraries\ESP32-BLE-MIDI" },
    @{ Name = "NimBLE-Arduino"; Src = "libraries\NimBLE-Arduino" }
)

# ---- Helper: link one library -----------------------------------------------
function Link-Library {
    param(
        [string]$name,
        [string]$repoLib,
        [string]$symlinkPath
    )

    # Verify submodule/source is present
    if (!(Test-Path $repoLib)) {
        Write-Host "  SKIP $name — not found at:" -ForegroundColor Yellow
        Write-Host "         $repoLib" -ForegroundColor Yellow
        Write-Host "         (Run 'git submodule update --init --recursive' if this is a submodule)" -ForegroundColor Yellow
        Write-Host ""
        return
    }

    if (Test-Path $symlinkPath) {
        $item = Get-Item $symlinkPath -Force
        if ($item.LinkType -eq "SymbolicLink" -or $item.LinkType -eq "Junction") {
            # Resolve the actual target; on Windows $item.Target can be an array
            $target = if ($item.Target -is [array]) { $item.Target[0] } else { $item.Target }
            # Normalise both paths for comparison
            $targetNorm    = [System.IO.Path]::GetFullPath($target)
            $repoLibNorm   = [System.IO.Path]::GetFullPath($repoLib)
            if ($targetNorm -eq $repoLibNorm) {
                Write-Host "  ✓ $name already linked correctly" -ForegroundColor Green
                Write-Host ""
                return
            }
            Write-Host "  WARNING: $name symlink points to wrong location:" -ForegroundColor Yellow
            Write-Host "    Current:  $target" -ForegroundColor Yellow
            Write-Host "    Expected: $repoLib" -ForegroundColor Yellow
            Write-Host ""
            $response = Read-Host "  Remove and recreate? (y/n)"
            if ($response -ne "y") {
                Write-Host "  Skipped $name." -ForegroundColor Yellow
                Write-Host ""
                return
            }
            Remove-Item $symlinkPath -Force -Recurse
        } else {
            Write-Host "  ERROR: $name — a real folder/file already exists at:" -ForegroundColor Red
            Write-Host "    $symlinkPath" -ForegroundColor Red
            Write-Host "  Please remove it manually (it may be an old Arduino Library Manager install)." -ForegroundColor Yellow
            Write-Host ""
            return
        }
    }

    try {
        New-Item -ItemType SymbolicLink -Path $symlinkPath -Target $repoLib -ErrorAction Stop | Out-Null
        Write-Host "  ✓ Linked $name" -ForegroundColor Green
        Write-Host "      → $repoLib" -ForegroundColor Gray
        Write-Host ""
    } catch {
        Write-Host "  ERROR: Failed to create symlink for $name" -ForegroundColor Red
        Write-Host "  This script must be run as Administrator." -ForegroundColor Yellow
        Write-Host "  Error: $_" -ForegroundColor Red
        Write-Host ""
    }
}

# ---- Link all libraries -----------------------------------------------------
foreach ($lib in $libraries) {
    $repoLib     = Join-Path $PSScriptRoot $lib.Src
    $symlinkPath = Join-Path $arduinoLibs  $lib.Name
    Link-Library -name $lib.Name -repoLib $repoLib -symlinkPath $symlinkPath
}

Write-Host "Done! Restart Arduino IDE if it is currently open." -ForegroundColor Cyan
Write-Host ""
Read-Host "Press Enter to exit"
