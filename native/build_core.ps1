# Build ONLY dccore (no Godot) in Release and run its tests; also builds the dcgolden tool. Usage: powershell -File native\build_core.ps1
. "$PSScriptRoot\env.ps1"
$bd = "$PSScriptRoot\build\core_release"
cmake -S "$PSScriptRoot\dccore" -B $bd -G Ninja "-DCMAKE_BUILD_TYPE=Release" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $bd
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir $bd --output-on-failure
