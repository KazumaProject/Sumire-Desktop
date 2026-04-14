param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutputDir = "",

    [string]$CertificateThumbprint = "",
    [string]$PfxPath = "",
    [string]$PfxPassword = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

function Find-SignTool {
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (-not (Test-Path -LiteralPath $kitsRoot)) {
        throw "Windows Kits signtool.exe was not found. Install the Windows SDK."
    }

    $candidates = Get-ChildItem -LiteralPath $kitsRoot -Recurse -Filter signtool.exe |
        Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
        Sort-Object FullName -Descending

    if (-not $candidates) {
        $candidates = Get-ChildItem -LiteralPath $kitsRoot -Recurse -Filter signtool.exe |
            Sort-Object FullName -Descending
    }

    if (-not $candidates) {
        throw "signtool.exe was not found under $kitsRoot."
    }

    return $candidates[0].FullName
}

function Invoke-SignTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath
    )

    if (-not (Test-Path -LiteralPath $FilePath)) {
        throw "Missing file to sign: $FilePath"
    }

    $arguments = @("sign", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256")

    if (-not [string]::IsNullOrWhiteSpace($PfxPath)) {
        $arguments += @("/f", $PfxPath)
        if (-not [string]::IsNullOrWhiteSpace($PfxPassword)) {
            $arguments += @("/p", $PfxPassword)
        }
    }
    elseif (-not [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
        $arguments += @("/sha1", $CertificateThumbprint)
    }
    else {
        throw "Provide either -CertificateThumbprint or -PfxPath."
    }

    $arguments += $FilePath

    Write-Host "Signing $FilePath"
    & $script:SignTool @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed for $FilePath"
    }
}

function Test-Signed {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $FilePath
    if ($signature.Status -ne "Valid") {
        throw "Signature is not valid for $FilePath. Status: $($signature.Status)"
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "artifacts\release"
}

$buildDir = Join-Path $repoRoot (Join-Path $Platform $Configuration)
$payloadFiles = @(
    "Sumite-Desktop.dll",
    "SumireSettings.exe",
    "SumireUninstaller.exe",
    "SumireZenzService.exe",
    "SumireInstaller.exe"
)

$script:SignTool = Find-SignTool

foreach ($file in $payloadFiles) {
    Invoke-SignTool -FilePath (Join-Path $buildDir $file)
}

& (Join-Path $scriptDir "package-release.ps1") `
    -Version $Version `
    -Configuration $Configuration `
    -Platform $Platform `
    -OutputDir $OutputDir
if ($LASTEXITCODE -ne 0) {
    throw "package-release.ps1 failed."
}

$setupPath = Join-Path $OutputDir "Sumire-$Version-windows-$Platform-Setup.exe"
Invoke-SignTool -FilePath $setupPath

foreach ($file in $payloadFiles) {
    Test-Signed -FilePath (Join-Path $buildDir $file)
}
Test-Signed -FilePath $setupPath

Write-Host "Signed release asset:"
Write-Host $setupPath
