[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-release-check",
    [string]$ScanPath = ".\src"
)

$ErrorActionPreference = 'Stop'
$env:PATH = "F:\msys64\ucrt64\bin;F:\msys64\usr\bin;" + $env:PATH

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot

function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

try {
    $buildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
        $BuildDirectory
    } else {
        Join-Path $projectRoot $BuildDirectory
    }
    $buildPath = [System.IO.Path]::GetFullPath($buildPath)

    Write-Host "Configuring Release build..."
    Invoke-Native "cmake" @("-G", "Ninja", "-S", ".", "-B", $buildPath, "-DCMAKE_BUILD_TYPE=Release")
    Write-Host "Building Release binaries..."
    Invoke-Native "cmake" @("--build", $buildPath)

    $cliExecutable = Join-Path $buildPath "file_scanner.exe"
    $guiExecutable = Join-Path $buildPath "file_scanner_gui.exe"
    if (-not (Test-Path -LiteralPath $cliExecutable -PathType Leaf)) {
        throw "Release CLI executable was not created: $cliExecutable"
    }
    if (-not (Test-Path -LiteralPath $guiExecutable -PathType Leaf)) {
        throw "Release GUI executable was not created: $guiExecutable"
    }

    $csvPath = Join-Path $projectRoot "output\release-demo.csv"
    Write-Host "Running CLI demo..."
    Invoke-Native $cliExecutable @("--path", $ScanPath, "--threads", "4", "--output", $csvPath)

    Write-Host "Running smoke tests against Release CLI..."
    Invoke-Native "powershell.exe" @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        ".\tests\smoke_tests.ps1",
        "-Executable",
        $cliExecutable,
        "-SkipGui",
        "-SkipBenchmark"
    )

    Write-Host "Launching Release GUI..."
    $process = Start-Process -FilePath $guiExecutable -PassThru
    try {
        $handle = [IntPtr]::Zero
        for ($attempt = 0; $attempt -lt 30; $attempt++) {
            Start-Sleep -Milliseconds 100
            $process.Refresh()
            if ($process.MainWindowHandle -ne 0) {
                $handle = $process.MainWindowHandle
                break
            }
        }
        if ($handle -eq [IntPtr]::Zero) {
            throw "Release GUI window was not created."
        }
        $process.CloseMainWindow() | Out-Null
        $process.WaitForExit(3000) | Out-Null
        if (-not $process.HasExited -or $process.ExitCode -ne 0) {
            throw "Release GUI did not exit cleanly."
        }
    } finally {
        if ($process -and -not $process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            $process.WaitForExit(1000) | Out-Null
        }
    }

    Write-Host "Release check passed." -ForegroundColor Green
    Write-Host "CLI: $cliExecutable"
    Write-Host "GUI: $guiExecutable"
    Write-Host "CSV: $csvPath"
} finally {
    Pop-Location
}
