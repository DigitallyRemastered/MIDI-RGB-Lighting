# LightEngine Setup Script
# Run as Administrator to create symlink from Arduino libraries folder to project

Write-Host "LightEngine Library Setup" -ForegroundColor Cyan
Write-Host "=========================" -ForegroundColor Cyan
Write-Host ""

# Paths
$arduinoLibs = "$env:USERPROFILE\Documents\Arduino\libraries"
$repoLib = "$PSScriptRoot\libraries\LightEngine"

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

# Check if repo library exists
if (!(Test-Path $repoLib)) {
    Write-Host "ERROR: LightEngine library not found at:" -ForegroundColor Red
    Write-Host "  $repoLib" -ForegroundColor Red
    Write-Host ""
    Write-Host "Make sure you're running this script from the repo root." -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

# Check if symlink already exists
$symlinkPath = "$arduinoLibs\LightEngine"
if (Test-Path $symlinkPath) {
    # Check if it's already a symlink pointing to the right place
    $item = Get-Item $symlinkPath
    if ($item.LinkType -eq "SymbolicLink" -or $item.LinkType -eq "Junction") {
        $target = $item.Target
        if ($target -eq $repoLib) {
            Write-Host "✓ LightEngine library already linked correctly!" -ForegroundColor Green
            Write-Host ""
            Write-Host "Target: $repoLib" -ForegroundColor Gray
            Write-Host ""
            Read-Host "Press Enter to exit"
            exit 0
        } else {
            Write-Host "WARNING: LightEngine symlink exists but points to wrong location:" -ForegroundColor Yellow
            Write-Host "  Current: $target" -ForegroundColor Yellow
            Write-Host "  Expected: $repoLib" -ForegroundColor Yellow
            Write-Host ""
            $response = Read-Host "Remove and recreate? (y/n)"
            if ($response -ne "y") {
                Write-Host "Cancelled." -ForegroundColor Yellow
                Read-Host "Press Enter to exit"
                exit 1
            }
            Remove-Item $symlinkPath -Force
        }
    } else {
        Write-Host "ERROR: LightEngine folder exists but is not a symlink:" -ForegroundColor Red
        Write-Host "  $symlinkPath" -ForegroundColor Red
        Write-Host ""
        Write-Host "Please remove or rename this folder manually and run setup again." -ForegroundColor Yellow
        Write-Host ""
        Read-Host "Press Enter to exit"
        exit 1
    }
}

# Create symlink
Write-Host "Creating symlink..." -ForegroundColor Cyan
Write-Host "  From: $symlinkPath" -ForegroundColor Gray
Write-Host "  To:   $repoLib" -ForegroundColor Gray
Write-Host ""

try {
    New-Item -ItemType SymbolicLink -Path $symlinkPath -Target $repoLib -ErrorAction Stop | Out-Null
    Write-Host "✓ Successfully linked LightEngine library!" -ForegroundColor Green
    Write-Host ""
    Write-Host "You can now use #include <LightEngine.h> in your Arduino sketches." -ForegroundColor Green
    Write-Host "Restart Arduino IDE if it's currently open." -ForegroundColor Yellow
} catch {
    Write-Host "ERROR: Failed to create symlink" -ForegroundColor Red
    Write-Host ""
    Write-Host "This script must be run as Administrator." -ForegroundColor Yellow
    Write-Host "Right-click PowerShell and select 'Run as Administrator', then try again." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Error details: $_" -ForegroundColor Red
}

Write-Host ""
Read-Host "Press Enter to exit"
