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

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,

        [Parameter(Mandatory = $true)]
        [string]$TargetPath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)

    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and
        -not $baseFull.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
        $baseFull += [System.IO.Path]::DirectorySeparatorChar
    }

    if (-not $targetFull.StartsWith($baseFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is not under payload root: $TargetPath"
    }

    return $targetFull.Substring($baseFull.Length)
}

function Write-EmbeddedPayload {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SetupPath,

        [Parameter(Mandatory = $true)]
        [string]$PayloadRoot
    )

    $magicBytes = [System.Text.Encoding]::ASCII.GetBytes("SUMIRE_PAYLOAD1")
    $payloadFiles = Get-ChildItem -LiteralPath $PayloadRoot -Recurse -File | Sort-Object FullName
    $entries = @()

    $setupStream = [System.IO.File]::Open($SetupPath, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    try {
        foreach ($file in $payloadFiles) {
            $relativePath = (Get-RelativePath -BasePath $PayloadRoot -TargetPath $file.FullName).Replace('\', '/')
            $offset = [uint64]$setupStream.Position

            $inputStream = [System.IO.File]::OpenRead($file.FullName)
            try {
                $inputStream.CopyTo($setupStream)
            }
            finally {
                $inputStream.Dispose()
            }

            $entries += [PSCustomObject]@{
                RelativePath = $relativePath
                Offset = $offset
                Size = [uint64]$file.Length
            }
        }

        $tableStream = New-Object System.IO.MemoryStream
        $tableWriter = New-Object System.IO.BinaryWriter($tableStream, [System.Text.Encoding]::UTF8, $true)
        try {
            $tableWriter.Write([uint32]1)
            $tableWriter.Write([uint32]$entries.Count)
            foreach ($entry in $entries) {
                $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($entry.RelativePath)
                $tableWriter.Write([uint32]$pathBytes.Length)
                $tableWriter.Write($pathBytes)
                $tableWriter.Write([uint64]$entry.Offset)
                $tableWriter.Write([uint64]$entry.Size)
            }
            $tableWriter.Flush()

            $tableBytes = $tableStream.ToArray()
            $setupStream.Write($tableBytes, 0, $tableBytes.Length)

            $footerWriter = New-Object System.IO.BinaryWriter($setupStream, [System.Text.Encoding]::ASCII, $true)
            try {
                $footerWriter.Write([uint64]$tableBytes.Length)
                $footerWriter.Write($magicBytes)
                $footerWriter.Flush()
            }
            finally {
                $footerWriter.Dispose()
            }
        }
        finally {
            $tableWriter.Dispose()
            $tableStream.Dispose()
        }
    }
    finally {
        $setupStream.Dispose()
    }
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
$payloadStage = Join-Path $resolvedOutputDir "$bundlePrefix-payload"
$setupExe = Join-Path $resolvedOutputDir "$bundlePrefix-Setup.exe"

if (Test-Path -LiteralPath $payloadStage) {
    Remove-Item -LiteralPath $payloadStage -Recurse -Force
}
if (Test-Path -LiteralPath $setupExe) {
    Remove-Item -LiteralPath $setupExe -Force
}

New-Item -ItemType Directory -Path $payloadStage -Force | Out-Null

$payloadFiles = @(
    "Sumite-Desktop.dll",
    "SumireSettings.exe",
    "SumireUninstaller.exe",
    "SumireZenzService.exe"
)

foreach ($file in $payloadFiles) {
    Copy-FileIfExists -Source (Join-Path $buildDir $file) -Destination $payloadStage
}

Copy-FileIfExists -Source (Join-Path $repoRoot "romaji-hiragana.tsv") -Destination $payloadStage
Copy-DirectoryIfExists -Source (Join-Path $repoRoot "dictionaries") -Destination (Join-Path $payloadStage "dictionaries")
Copy-FileIfExists -Source (Join-Path $buildDir "SumireInstaller.exe") -Destination $setupExe
Write-EmbeddedPayload -SetupPath $setupExe -PayloadRoot $payloadStage
Remove-Item -LiteralPath $payloadStage -Recurse -Force

Write-Host "Created release asset:"
Write-Host $setupExe
