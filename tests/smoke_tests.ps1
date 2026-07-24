[CmdletBinding()]
param(
    [string]$Executable = "",
    [switch]$SkipGui,
    [switch]$SkipBenchmark
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot

$passed = 0
$failed = 0
$tempRoot = $null

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-Scanner {
    param([string[]]$Arguments)

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @(& $script:scannerExecutable @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    [pscustomobject]@{
        ExitCode = $exitCode
        Output = ($lines | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    }
}

function Get-IntegerMetric {
    param(
        [string]$Output,
        [string]$Label
    )
    $match = [regex]::Match($Output, [regex]::Escape($Label) + ':\s*(\d+)')
    Assert-Condition $match.Success "Metric not found: $Label"
    return [int64]$match.Groups[1].Value
}

function Get-DecimalMetric {
    param(
        [string]$Output,
        [string]$Label
    )
    $match = [regex]::Match($Output, [regex]::Escape($Label) + ':\s*([0-9]+(?:\.[0-9]+)?)')
    Assert-Condition $match.Success "Metric not found: $Label"
    return [double]$match.Groups[1].Value
}

function Run-Test {
    param(
        [string]$Name,
        [scriptblock]$Action
    )
    try {
        & $Action
        Write-Host "[PASS] $Name" -ForegroundColor Green
        $script:passed++
    } catch {
        Write-Host "[FAIL] $Name - $($_.Exception.Message)" -ForegroundColor Red
        $script:failed++
    }
}

try {
    if ([string]::IsNullOrWhiteSpace($Executable)) {
        $scannerExecutable = Join-Path $projectRoot "build\file_scanner.exe"
    } elseif ([System.IO.Path]::IsPathRooted($Executable)) {
        $scannerExecutable = $Executable
    } else {
        $scannerExecutable = Join-Path $projectRoot $Executable
    }
    $scannerExecutable = [System.IO.Path]::GetFullPath($scannerExecutable)
    Assert-Condition (Test-Path -LiteralPath $scannerExecutable -PathType Leaf) "Scanner executable not found: $scannerExecutable"

    $guiExecutable = Join-Path $projectRoot "build\file_scanner_gui.exe"
    $sourcePath = Join-Path $projectRoot "src"
    $fileLabel = -join @([char]0x6587, [char]0x4ef6, [char]0x6570)
    $sizeLabel = -join @([char]0x603b, [char]0x5927, [char]0x5c0f)
    $errorLabel = -join @([char]0x9519, [char]0x8bef, [char]0x6570)

    $tempRoot = Join-Path $projectRoot ("output\.smoke-tests-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

    Run-Test "basic scan" {
        $result = Invoke-Scanner @("--path", $sourcePath, "--threads", "4")
        Assert-Condition ($result.ExitCode -eq 0) "basic scan exit code: $($result.ExitCode)"
        $script:baselineFileCount = Get-IntegerMetric $result.Output $fileLabel
        $script:baselineSize = Get-DecimalMetric $result.Output $sizeLabel
        $script:baselineErrorCount = Get-IntegerMetric $result.Output $errorLabel
        Assert-Condition ($script:baselineFileCount -gt 0) "basic scan returned no files"
        Assert-Condition ($script:baselineErrorCount -eq 0) "basic scan returned errors"
    }

    Run-Test "thread count consistency 1 4 64" {
        foreach ($threadCount in @(1, 4, 64)) {
            $result = Invoke-Scanner @("--path", $sourcePath, "--threads", "$threadCount")
            Assert-Condition ($result.ExitCode -eq 0) "thread $threadCount exit code: $($result.ExitCode)"
            Assert-Condition ((Get-IntegerMetric $result.Output $fileLabel) -eq $script:baselineFileCount) "thread $threadCount file count differs"
            $sizeDifference = [math]::Abs((Get-DecimalMetric $result.Output $sizeLabel) - $script:baselineSize)
            Assert-Condition ($sizeDifference -lt 0.0001) "thread $threadCount size differs"
            Assert-Condition ((Get-IntegerMetric $result.Output $errorLabel) -eq $script:baselineErrorCount) "thread $threadCount error count differs"
        }
    }

    Run-Test "filtered scan and CSV row count" {
        $csvPath = Join-Path $tempRoot "filtered.csv"
        $result = Invoke-Scanner @("--path", $sourcePath, "--threads", "4", "--ext", "cpp", "--output", $csvPath)
        Assert-Condition ($result.ExitCode -eq 0) "filtered scan exit code: $($result.ExitCode)"
        Assert-Condition (Test-Path -LiteralPath $csvPath -PathType Leaf) "CSV file was not created"
        $rows = @(Import-Csv -Encoding UTF8 -LiteralPath $csvPath)
        Assert-Condition ($rows.Count -eq (Get-IntegerMetric $result.Output $fileLabel)) "CSV row count differs from metadata count"
        Assert-Condition ($rows.Count -gt 0) "filtered CSV is empty"
    }

    Run-Test "empty directory" {
        $emptyPath = Join-Path $tempRoot "empty"
        New-Item -ItemType Directory -Force -Path $emptyPath | Out-Null
        $result = Invoke-Scanner @("--path", $emptyPath, "--threads", "4")
        Assert-Condition ($result.ExitCode -eq 0) "empty directory exit code: $($result.ExitCode)"
        Assert-Condition ((Get-IntegerMetric $result.Output $fileLabel) -eq 0) "empty directory returned files"
        Assert-Condition ((Get-IntegerMetric $result.Output $errorLabel) -eq 0) "empty directory returned errors"
    }

    Run-Test "invalid directory and error log" {
        $missingPath = Join-Path $tempRoot "missing-directory"
        $logPath = Join-Path $tempRoot "errors.log"
        $result = Invoke-Scanner @("--path", $missingPath, "--threads", "4", "--log", $logPath)
        Assert-Condition ($result.ExitCode -eq 1) "invalid directory exit code: $($result.ExitCode)"
        Assert-Condition (Test-Path -LiteralPath $logPath -PathType Leaf) "error log was not created"
        Assert-Condition ((Get-Item -LiteralPath $logPath).Length -gt 0) "error log is empty"
    }

    Run-Test "invalid command line arguments" {
        $zeroThreads = Invoke-Scanner @("--path", $sourcePath, "--threads", "0")
        $missingOutput = Invoke-Scanner @("--path", $sourcePath, "--output")
        $unknownOption = Invoke-Scanner @("--path", $sourcePath, "--unknown")
        Assert-Condition ($zeroThreads.ExitCode -eq 1) "zero thread count was accepted"
        Assert-Condition ($missingOutput.ExitCode -eq 1) "missing output value was accepted"
        Assert-Condition ($unknownOption.ExitCode -eq 1) "unknown option was accepted"
    }

    if (-not $SkipBenchmark) {
        Run-Test "1 2 4 8 benchmark" {
            $benchmarkPath = Join-Path $tempRoot "benchmark.csv"
            $benchmarkOutput = @(
                powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\benchmark.ps1" `
                    -ScanPath ".\src" -RunsPerThread 1 -OutputFile $benchmarkPath 2>&1
            )
            $benchmarkExit = $LASTEXITCODE
            Assert-Condition ($benchmarkExit -eq 0) "benchmark exit code: $benchmarkExit"
            $rows = @(Import-Csv -Encoding UTF8 -LiteralPath $benchmarkPath)
            Assert-Condition ($rows.Count -eq 4) "benchmark row count is not 4"
            Assert-Condition ((($rows | ForEach-Object ThreadCount) -join ',') -eq '1,2,4,8') "benchmark thread groups differ"
        }
    }

    if (-not $SkipGui) {
        Run-Test "GUI launches and exits" {
            Assert-Condition (Test-Path -LiteralPath $guiExecutable -PathType Leaf) "GUI executable not found"
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
                Assert-Condition ($handle -ne [IntPtr]::Zero) "GUI window handle was not created"
                $process.CloseMainWindow() | Out-Null
                $process.WaitForExit(3000) | Out-Null
                Assert-Condition $process.HasExited "GUI did not exit"
                Assert-Condition ($process.ExitCode -eq 0) "GUI exit code: $($process.ExitCode)"
            } finally {
                if ($process -and -not $process.HasExited) {
                    $process.CloseMainWindow() | Out-Null
                    $process.WaitForExit(1000) | Out-Null
                }
            }
        }
    }

    Write-Host ""
    Write-Host "Passed: $passed  Failed: $failed"
    if ($failed -gt 0) {
        exit 1
    }
} finally {
    if ($tempRoot -and (Test-Path -LiteralPath $tempRoot)) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
