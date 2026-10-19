# Restart the main PCSX2 on the patched ISO. Prints the new process id (the caller waits for PINE).
# PATH is filtered for THIS process only: C:\Program Files\Tesseract-OCR ships its own libharfbuzz-0.dll, which PCSX2 then loads by mistake
# ("Entry Point Not Found: DllMain") and the modal error blocks start-up. Nothing on the system is changed.
param([string]$Iso = "$PSScriptRoot\..\iso\Dark Cloud (USA).patched.iso")
Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3
$env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and ($_ -notlike '*Tesseract*') }) -join ';'
$p = Start-Process -FilePath "$PSScriptRoot\..\tools\pcsx2\pcsx2-qt.exe" -ArgumentList @("-fastboot", "--", "`"$Iso`"") -WorkingDirectory "$PSScriptRoot\..\tools\pcsx2" -PassThru
Write-Output $p.Id
