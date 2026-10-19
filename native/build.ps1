# Build dccore tests + the GDExtension. Usage: powershell -File native\build.ps1 [-Target template_debug|template_release] [-Clean]
param([string]$Target = "template_debug", [switch]$Clean)
. "$PSScriptRoot\env.ps1"
$bd = "$PSScriptRoot\build\$Target"
if ($Clean -and (Test-Path $bd)) { Remove-Item $bd -Recurse -Force }
$type = if ($Target -eq "template_release") { "Release" } else { "Debug" }
cmake -S $PSScriptRoot -B $bd -G Ninja "-DCMAKE_BUILD_TYPE=$type" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DGODOTCPP_TARGET=$Target" "-DGODOTCPP_API_VERSION=4.7"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $bd
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir "$bd\dccore" --output-on-failure
