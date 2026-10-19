# Re-check every C++ reader against the Python reference on the whole ISO (about 1 minute). Exit code 0 = identical.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File native\tools\run_golden.ps1 [-Iso "iso\Dark Cloud (USA).iso"] [-Regenerate]
param([string]$Iso = "iso\Dark Cloud (USA).iso", [switch]$Regenerate)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$g = "native\dccore\tests\golden"
powershell -NoProfile -ExecutionPolicy Bypass -File native\build_core.ps1 | Select-Object -Last 3
if ($Regenerate -or -not (Test-Path "$g\golden_py.txt")) { python -I native\tools\make_golden.py $Iso "$g\golden_py.txt" | Select-Object -Last 1 }
& native\build\core_release\dcgolden.exe $Iso "$g\golden_cpp.txt" 2>&1 | Select-Object -Last 1
python -I native\tools\compare_golden.py "$g\golden_py.txt" "$g\golden_cpp.txt"
$rc = $LASTEXITCODE
Remove-Item "$g\golden_cpp.txt" -ErrorAction SilentlyContinue
exit $rc
