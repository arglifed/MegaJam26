<#
.SYNOPSIS
    Automated Win64 packaging and submission archive script for SPLICE // DIRECTOR'S CUT (MegaJam26).

.DESCRIPTION
    Executes Unreal Automation Tool (RunUAT.bat BuildCookRun) to build, cook, stage, pak, and archive
    a playable Win64 Development build of MegaJam26, then compresses the staged Windows directory
    into a submission-ready zip archive at Saved/SPD_DirectorsCut.zip.

    Executing `.\Scripts\package_build.ps1` from the repository root produces a submission-ready zip:
        Saved/SPD_DirectorsCut.zip

.PARAMETER EnginePath
    Optional override for the Unreal Engine installation root directory (e.g. "C:\Program Files\Epic Games\UE_5.8").
    If omitted, the script automatically resolves the EngineAssociation from MegaJam26.uproject.

.PARAMETER MaxParallelActions
    Maximum parallel compile/cook actions passed to UAT (default: 3 to prevent OOM on 16 GB machines).

.EXAMPLE
    .\Scripts\package_build.ps1
    .\Scripts\package_build.ps1 -EnginePath "C:\Program Files\Epic Games\UE_5.8"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$EnginePath = "",

    [Parameter(Mandatory = $false)]
    [int]$MaxParallelActions = 3
)

$ErrorActionPreference = "Stop"
$StartTime = Get-Date

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectFile = Join-Path $RepoRoot "MegaJam26.uproject"
$SavedDir = Join-Path $RepoRoot "Saved"
$ArchiveDir = Join-Path $SavedDir "StagedBuilds"
$OutputWindowsDir = Join-Path $ArchiveDir "Windows"
$OutputZipPath = Join-Path $SavedDir "SPD_DirectorsCut.zip"

if (-not (Test-Path $ProjectFile)) {
    throw "Project file not found at: $ProjectFile"
}

# 1. Resolve Unreal Engine Path
if ([string]::IsNullOrWhiteSpace($EnginePath)) {
    if (-not [string]::IsNullOrWhiteSpace($env:UE_ROOT) -and (Test-Path $env:UE_ROOT)) {
        $EnginePath = $env:UE_ROOT
    } else {
        $UProjectJson = Get-Content -Raw -Path $ProjectFile | ConvertFrom-Json
        $EngineAssoc = $UProjectJson.EngineAssociation
        $CandidatePaths = @(
            "C:\Program Files\Epic Games\UE_$EngineAssoc",
            "C:\Program Files\Epic Games\UE_5.8",
            "C:\Program Files\Epic Games\UE_5.7",
            "C:\Program Files\Epic Games\UE_5.6",
            "C:\Program Files\Epic Games\UE_5.5"
        )
        foreach ($Candidate in $CandidatePaths) {
            if (Test-Path (Join-Path $Candidate "Engine\Build\BatchFiles\RunUAT.bat")) {
                $EnginePath = $Candidate
                break
            }
        }
    }
}

$RunUAT = Join-Path $EnginePath "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $RunUAT)) {
    throw "RunUAT.bat could not be found at '$RunUAT'. Pass -EnginePath 'C:\Path\To\UE_5.x' or set `$env:UE_ROOT."
}

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  SPLICE // DIRECTOR'S CUT - Win64 Submission Packaging Script" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "Project File : $ProjectFile"
Write-Host "Engine Path  : $EnginePath"
Write-Host "Archive Dir  : $ArchiveDir"
Write-Host "Output Zip   : $OutputZipPath"
Write-Host ""

# Clean previous staged Windows output to ensure a fresh archive
if (Test-Path $OutputWindowsDir) {
    Write-Host "Cleaning previous staged Windows directory: $OutputWindowsDir" -ForegroundColor Yellow
    Remove-Item -Path $OutputWindowsDir -Recurse -Force
}
if (-not (Test-Path $ArchiveDir)) {
    New-Item -ItemType Directory -Path $ArchiveDir -Force | Out-Null
}

# 2. Execute RunUAT.bat BuildCookRun
$UATArgs = @(
    "BuildCookRun",
    "-project=`"$ProjectFile`"",
    "-noP4",
    "-platform=Win64",
    "-clientconfig=Development",
    "-build",
    "-cook",
    "-stage",
    "-pak",
    "-archive",
    "-archivedirectory=`"$ArchiveDir`"",
    "-MaxParallelActions=$MaxParallelActions"
)

Write-Host "Running UAT: `"$RunUAT`" $($UATArgs -join ' ')" -ForegroundColor Green
& $RunUAT @UATArgs
if ($LASTEXITCODE -ne 0) {
    throw "RunUAT.bat BuildCookRun failed with exit code $LASTEXITCODE."
}

# Resolve packaged Windows output folder (UE5 uses 'Windows', UE4/legacy uses 'WindowsNoEditor')
if (-not (Test-Path $OutputWindowsDir)) {
    $LegacyDir = Join-Path $ArchiveDir "WindowsNoEditor"
    if (Test-Path $LegacyDir) {
        $OutputWindowsDir = $LegacyDir
    } else {
        throw "Expected packaged output directory not found at '$OutputWindowsDir'."
    }
}

# 3. Compress staged Windows directory into Saved/SPD_DirectorsCut.zip
if (Test-Path $OutputZipPath) {
    Remove-Item -Path $OutputZipPath -Force
}

Write-Host "Compressing '$OutputWindowsDir' -> '$OutputZipPath'..." -ForegroundColor Green
Compress-Archive -Path (Join-Path $OutputWindowsDir "*") -DestinationPath $OutputZipPath -CompressionLevel Optimal -Force

$Elapsed = (Get-Date) - $StartTime
$ZipItem = Get-Item $OutputZipPath
$ZipSizeMB = [math]::Round($ZipItem.Length / 1MB, 2)

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  PACKAGING COMPLETE" -ForegroundColor Green
Write-Host "  Archive : $OutputZipPath"
Write-Host "  Size    : $ZipSizeMB MB"
Write-Host "  Elapsed : $([math]::Round($Elapsed.TotalMinutes, 2)) minutes"
Write-Host "================================================================" -ForegroundColor Cyan
