@echo off
rem Ghidra 12.1.4 with the bundled JDK 21 and the PS2 (Emotion Engine) plugin.
rem Open ghidra\DarkCloud.gpr from File > Open Project.
set "JAVA_HOME=%~dp0tools\jdk-21.0.12.1+1"
start "" "%~dp0tools\ghidra_12.1.4_PUBLIC\ghidraRun.bat"
