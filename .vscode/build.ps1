$ErrorActionPreference = 'Stop'

$env:PATH = "F:\\msys64\\ucrt64\\bin;F:\\msys64\\usr\\bin;" + $env:PATH
$compiler = 'F:\msys64\ucrt64\bin\g++.exe'
$buildRoot = 'build'

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$compilerArgs = @(
    '-fdiagnostics-color=always'
    '-std=c++17'
    '-Wall'
    '-Wextra'
    '-Wpedantic'
    '-g'
    '-static'
    'src/main.cpp'
    'src/thread_pool.cpp'
    '-I'
    'include'
    '-municode'
    '-o'
    'build/file_scanner.exe'
)

& $compiler @compilerArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
