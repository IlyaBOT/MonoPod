[CmdletBinding()]
param(
    [ValidateSet('s3','esp32')][string]$Board = 's3',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [string]$ArduinoCli = 'arduino-cli'
)
$ErrorActionPreference = 'Stop'

$desktopRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $desktopRoot
$sketch = Join-Path $repoRoot 'firmware\DIY_iPod_Mono'
$buildPath = Join-Path $desktopRoot ("work\build-" + $Board)
if (-not (Test-Path -LiteralPath $sketch -PathType Container)) {
    throw "Firmware sketch folder not found: $sketch"
}
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

if ($Board -eq 's3') {
    $fqbn = 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB'
} else {
    $fqbn = 'esp32:esp32:esp32:PartitionScheme=huge_app'
}
Write-Host "Compiling firmware: $sketch"
Write-Host "Board: $Board ($fqbn)"
& $ArduinoCli compile --jobs $Jobs --fqbn $fqbn --build-path $buildPath $sketch
if ($LASTEXITCODE -ne 0) { throw "Arduino compile failed with exit code $LASTEXITCODE" }
Write-Host "Build complete: $buildPath"
Write-Host "For OTA use a compatible partition layout; see firmware/tools/partitions/."
