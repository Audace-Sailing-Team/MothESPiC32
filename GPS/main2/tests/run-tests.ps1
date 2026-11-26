param([string]$Compiler = "gcc")

$ErrorActionPreference = "Stop"
$firmwareDir = Split-Path -Parent $PSScriptRoot
$testBuildDir = Join-Path $firmwareDir "build/tests"
New-Item -ItemType Directory -Force -Path $testBuildDir | Out-Null
$testExecutable = Join-Path $testBuildDir "gps_nmea_tests.exe"

& $Compiler -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 `
    "-I$firmwareDir/main" "$firmwareDir/main/gps_nmea.c" `
    "$PSScriptRoot/test_gps_nmea.c" -lm -o $testExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $testExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $testExecutable --file "$firmwareDir/samples/demo.nmea"
exit $LASTEXITCODE
