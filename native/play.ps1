# Starts the game (title screen -> village -> dungeons, with co-op) from your own disc image.
#   powershell -File native\play.ps1 [--iso "path\to\Dark Cloud (USA).iso"] [--coop host] [--coop join:<ip>] ...
# The game reads the disc image directly (default: iso\Dark Cloud (USA).iso next to the native folder); nothing is copied out of it.
param([Parameter(ValueFromRemainingArguments = $true)] $GameArgs)
. "$PSScriptRoot\env.ps1"
& $env:GODOT_EDITOR --path "$PSScriptRoot\godot" -- @GameArgs
