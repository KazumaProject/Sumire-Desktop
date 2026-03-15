param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

function Copy-DirectoryIfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

function Copy-FileIfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Missing required file: $Source"
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "artifacts"
}

$resolvedOutputDir = $OutputDir
if (Test-Path -LiteralPath $resolvedOutputDir) {
    $resolvedOutputDir = (Resolve-Path -LiteralPath $resolvedOutputDir).Path
} else {
    New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null
    $resolvedOutputDir = (Resolve-Path -LiteralPath $resolvedOutputDir).Path
}

$buildDir = Join-Path $repoRoot (Join-Path $Platform $Configuration)
$bundlePrefix = "Sumire-$Version-windows-$Platform"
$installerStage = Join-Path $resolvedOutputDir "$bundlePrefix-installer"
$binariesStage = Join-Path $resolvedOutputDir "$bundlePrefix-binaries"

if (Test-Path -LiteralPath $installerStage) {
    Remove-Item -LiteralPath $installerStage -Recurse -Force
}
if (Test-Path -LiteralPath $binariesStage) {
    Remove-Item -LiteralPath $binariesStage -Recurse -Force
}

New-Item -ItemType Directory -Path $installerStage -Force | Out-Null
New-Item -ItemType Directory -Path $binariesStage -Force | Out-Null

$sharedFiles = @(
    "Sumite-Desktop.dll",
    "SumireSettings.exe",
    "SumireUninstaller.exe",
    "SumireZenzService.exe"
)

foreach ($file in $sharedFiles) {
    $source = Join-Path $buildDir $file
    Copy-FileIfExists -Source $source -Destination $installerStage
    Copy-FileIfExists -Source $source -Destination $binariesStage
}

Copy-FileIfExists -Source (Join-Path $buildDir "SumireInstaller.exe") -Destination $installerStage
Copy-FileIfExists -Source (Join-Path $repoRoot "romaji-hiragana.tsv") -Destination $installerStage
Copy-FileIfExists -Source (Join-Path $repoRoot "romaji-hiragana.tsv") -Destination $binariesStage
Copy-FileIfExists -Source (Join-Path $repoRoot "README.md") -Destination $installerStage
Copy-FileIfExists -Source (Join-Path $repoRoot "README.md") -Destination $binariesStage
Copy-FileIfExists -Source (Join-Path $repoRoot "README.ja.md") -Destination $installerStage
Copy-FileIfExists -Source (Join-Path $repoRoot "README.ja.md") -Destination $binariesStage
Copy-FileIfExists -Source (Join-Path $repoRoot "README.en.md") -Destination $installerStage
Copy-FileIfExists -Source (Join-Path $repoRoot "README.en.md") -Destination $binariesStage
Copy-FileIfExists -Source (Join-Path $repoRoot "LICENSE") -Destination $installerStage
Copy-FileIfExists -Source (Join-Path $repoRoot "LICENSE") -Destination $binariesStage

Copy-DirectoryIfExists -Source (Join-Path $repoRoot "dictionaries") -Destination (Join-Path $installerStage "dictionaries")
Copy-DirectoryIfExists -Source (Join-Path $repoRoot "dictionaries") -Destination (Join-Path $binariesStage "dictionaries")

$installerZip = Join-Path $resolvedOutputDir "$bundlePrefix-installer.zip"
$binariesZip = Join-Path $resolvedOutputDir "$bundlePrefix-binaries.zip"

if (Test-Path -LiteralPath $installerZip) {
    Remove-Item -LiteralPath $installerZip -Force
}
if (Test-Path -LiteralPath $binariesZip) {
    Remove-Item -LiteralPath $binariesZip -Force
}

Compress-Archive -Path (Join-Path $installerStage "*") -DestinationPath $installerZip -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $binariesStage "*") -DestinationPath $binariesZip -CompressionLevel Optimal

Write-Host "Created release assets:"
Write-Host $installerZip
Write-Host $binariesZip
