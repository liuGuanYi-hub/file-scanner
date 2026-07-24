$ErrorActionPreference = 'Stop'

$env:PATH = "F:\\msys64\\ucrt64\\bin;F:\\msys64\\usr\\bin;" + $env:PATH
$compiler = 'F:\msys64\ucrt64\bin\g++.exe'
$buildRoot = 'build'

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$commonArgs = @(
    '-fdiagnostics-color=always'
    '-std=c++17'
    '-Wall'
    '-Wextra'
    '-Wpedantic'
    '-g'
    '-static'
    '-municode'
    'src/csv_exporter.cpp'
    'src/error_logger.cpp'
    'src/scan_engine.cpp'
    'src/thread_pool.cpp'
    '-I'
    'include'
)

$cli_args = $commonArgs + @(
    'src/main.cpp'
    '-o'
    'build/file_scanner.exe'
)

& $compiler @cli_args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$gui_args = $commonArgs + @(
    'src/gui_main_win32.cpp'
    '-mwindows'
    '-lcomctl32'
    '-lshell32'
    '-lole32'
    '-o'
    'build/file_scanner_gui.exe'
)

& $compiler @gui_args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
