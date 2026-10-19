# Dot-source this in PowerShell to put the project's portable toolchain and Godot on PATH for this session only:  . native\env.ps1
$root = Split-Path -Parent $PSScriptRoot
$env:PATH = "$root\tools\llvm-mingw\bin;$root\tools\cmake\bin;$root\tools\ninja;$env:PATH"
$env:GODOT = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe\Godot_v4.7.2-stable_win64_console.exe"
$env:GODOT_EDITOR = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe\Godot_v4.7.2-stable_win64.exe"
