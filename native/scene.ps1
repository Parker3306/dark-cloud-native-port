# Runs one of the test scenes with the console build of Godot (prints to this window).
#   powershell -File native\scene.ps1 pad_test.tscn --seconds 30        (the DualShock as the game gets it)
#   powershell -File native\scene.ps1 town_play.tscn --town e01 --event -1 --complete 0 --live-shots 5
param([Parameter(Mandatory = $true)][string]$Scene, [Parameter(ValueFromRemainingArguments = $true)] $SceneArgs)
. "$PSScriptRoot\env.ps1"
& $env:GODOT --path "$PSScriptRoot\godot" "res://$Scene" -- @SceneArgs
