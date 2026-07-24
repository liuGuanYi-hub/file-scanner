[CmdletBinding()]
param(
    [string]$ScanPath = ".",
    [string]$ThreadCounts = "1,2,4,8",
    [int]$RunsPerThread = 3,
    [string]$Executable = "",
    [string]$OutputFile = ".\output\benchmark.csv"
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot

try {
    if ($RunsPerThread -lt 1) {
        throw "RunsPerThread must be at least 1."
    }
    $threadCountsToRun = @(
        $ThreadCounts -split ',' | ForEach-Object {
            $token = $_.Trim()
            if ([string]::IsNullOrWhiteSpace($token)) {
                throw "ThreadCounts contains an empty value."
            }
            try {
                [int]$token
            } catch {
                throw "ThreadCounts contains a non-integer value: $token"
            }
        }
    )
    if ($threadCountsToRun.Count -eq 0) {
        throw "ThreadCounts cannot be empty."
    }

    $seenThreads = @{}
    foreach ($threadCount in $threadCountsToRun) {
        if ($threadCount -lt 1 -or $threadCount -gt 64) {
            throw "Thread count $threadCount is outside the allowed range 1-64."
        }
        if ($seenThreads.ContainsKey($threadCount)) {
            throw "Thread count $threadCount is duplicated."
        }
        $seenThreads[$threadCount] = $true
    }

    $resolvedScanPath = (Resolve-Path -LiteralPath $ScanPath -ErrorAction Stop).Path

    if ([string]::IsNullOrWhiteSpace($Executable)) {
        $executablePath = Join-Path $projectRoot "build\file_scanner.exe"
    } elseif ([System.IO.Path]::IsPathRooted($Executable)) {
        $executablePath = $Executable
    } else {
        $executablePath = Join-Path (Get-Location) $Executable
    }
    $executablePath = [System.IO.Path]::GetFullPath($executablePath)
    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Scanner executable not found: $executablePath. Run .\.vscode\build.ps1 first."
    }

    if ([System.IO.Path]::IsPathRooted($OutputFile)) {
        $outputPath = $OutputFile
    } else {
        $outputPath = Join-Path $projectRoot $OutputFile
    }
    $outputPath = [System.IO.Path]::GetFullPath($outputPath)
    $outputDirectory = Split-Path -Parent $outputPath
    if ($outputDirectory) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }

    $fileLabel = -join @([char]0x6587, [char]0x4ef6, [char]0x6570)
    $sizeLabel = -join @([char]0x603b, [char]0x5927, [char]0x5c0f)
    $errorLabel = -join @([char]0x9519, [char]0x8bef, [char]0x6570)
    $timeLabel = -join @([char]0x8017, [char]0x65f6)
    $filePattern = [regex]::Escape($fileLabel) + ':\s*(\d+)'
    $sizePattern = [regex]::Escape($sizeLabel) + ':\s*([0-9]+(?:\.[0-9]+)?)\s+MiB'
    $errorPattern = [regex]::Escape($errorLabel) + ':\s*(\d+)'
    $timePattern = [regex]::Escape($timeLabel) + ':\s*([0-9]+(?:\.[0-9]+)?)\s*' + [char]0x79d2

    $benchmarkRows = @()
    foreach ($threadCount in $threadCountsToRun) {
        $runs = @()
        for ($runIndex = 1; $runIndex -le $RunsPerThread; $runIndex++) {
            $timer = [System.Diagnostics.Stopwatch]::StartNew()
            $rawOutput = @(
                & $executablePath --path $resolvedScanPath --threads $threadCount 2>&1
            )
            $exitCode = $LASTEXITCODE
            $timer.Stop()
            $capturedOutput = ($rawOutput | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine

            if ($exitCode -ne 0) {
                throw "Thread count $threadCount run $runIndex failed (exit code $exitCode):`n$capturedOutput"
            }

            $fileMatch = [regex]::Match($capturedOutput, $filePattern)
            $sizeMatch = [regex]::Match($capturedOutput, $sizePattern)
            $errorMatch = [regex]::Match($capturedOutput, $errorPattern)
            $reportedTimeMatch = [regex]::Match($capturedOutput, $timePattern)
            if (-not $fileMatch.Success -or -not $sizeMatch.Success -or
                -not $errorMatch.Success -or -not $reportedTimeMatch.Success) {
                throw "Could not parse thread count $threadCount run $runIndex output:`n$capturedOutput"
            }

            $runs += [pscustomobject]@{
                FileCount = [int64]$fileMatch.Groups[1].Value
                TotalSizeMiB = [double]$sizeMatch.Groups[1].Value
                ErrorCount = [int64]$errorMatch.Groups[1].Value
                ReportedSeconds = [double]$reportedTimeMatch.Groups[1].Value
                WallClockSeconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
            }
        }

        $firstRun = $runs[0]
        foreach ($run in $runs) {
            if ($run.FileCount -ne $firstRun.FileCount -or
                [math]::Abs($run.TotalSizeMiB - $firstRun.TotalSizeMiB) -gt 0.0001 -or
                $run.ErrorCount -ne $firstRun.ErrorCount) {
                throw "Repeated results for thread count $threadCount are inconsistent."
            }
        }

        $averageWallClock = ($runs | Measure-Object -Property WallClockSeconds -Average).Average
        $minimumWallClock = ($runs | Measure-Object -Property WallClockSeconds -Minimum).Minimum
        $maximumWallClock = ($runs | Measure-Object -Property WallClockSeconds -Maximum).Maximum
        $averageReported = ($runs | Measure-Object -Property ReportedSeconds -Average).Average
        $fileThroughput = if ($averageWallClock -gt 0) {
            $firstRun.FileCount / $averageWallClock
        } else {
            0
        }
        $sizeThroughput = if ($averageWallClock -gt 0) {
            $firstRun.TotalSizeMiB / $averageWallClock
        } else {
            0
        }

        $benchmarkRows += [pscustomobject]@{
            ThreadCount = $threadCount
            RunCount = $RunsPerThread
            AverageWallClockSeconds = [math]::Round($averageWallClock, 3)
            FastestWallClockSeconds = [math]::Round($minimumWallClock, 3)
            SlowestWallClockSeconds = [math]::Round($maximumWallClock, 3)
            AverageReportedSeconds = [math]::Round($averageReported, 2)
            FileCount = $firstRun.FileCount
            TotalSizeMiB = [math]::Round($firstRun.TotalSizeMiB, 2)
            ErrorCount = $firstRun.ErrorCount
            FilesPerSecond = [math]::Round($fileThroughput, 2)
            MiBPerSecond = [math]::Round($sizeThroughput, 2)
        }
    }

    $benchmarkRows | Export-Csv -LiteralPath $outputPath -NoTypeInformation -Encoding UTF8

    Write-Host "Benchmark completed" -ForegroundColor Green
    Write-Host "Scan path: $resolvedScanPath"
    Write-Host "Result file: $outputPath"
    Write-Host ""
    $benchmarkRows |
        Format-Table ThreadCount, RunCount, AverageWallClockSeconds, FileCount,
        ErrorCount, FilesPerSecond, MiBPerSecond -AutoSize
} finally {
    Pop-Location
}
