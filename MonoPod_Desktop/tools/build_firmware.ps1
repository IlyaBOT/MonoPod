[CmdletBinding()]
param(
    [ValidateSet('s3','esp32')][string]$Board = 's3',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [string]$ArduinoCli,
    [string]$ConfigFile
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwareRoot = Join-Path (Split-Path -Parent $projectRoot) 'MonoPod_v0.10.0\DIY_iPod_Mono'
if (-not $ArduinoCli) {
    $cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cliCommand) { $ArduinoCli = $cliCommand.Source }
    else { $ArduinoCli = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' }
}
if (-not (Test-Path -LiteralPath $ArduinoCli -PathType Leaf)) { throw 'arduino-cli not found; provide -ArduinoCli.' }
if (-not $ConfigFile) {
    $ideConfig = Join-Path $env:USERPROFILE '.arduinoIDE\arduino-cli.yaml'
    if (Test-Path -LiteralPath $ideConfig -PathType Leaf) { $ConfigFile = $ideConfig }
}
if ($Board -eq 's3') {
    $fqbn = 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB'
    $buildFolder = 'build-s3'
} else {
    $fqbn = 'esp32:esp32:esp32:PartitionScheme=huge_app'
    $buildFolder = 'build-esp32'
}
$buildPath = Join-Path $projectRoot ('work\' + $buildFolder)
$logPath = Join-Path $projectRoot ('docs\build-' + $Board + '.log')
$timingPath = Join-Path $projectRoot ('docs\build-' + $Board + '-timing.json')
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
$cliArguments = @('compile','--jobs',"$Jobs",'--fqbn',$fqbn,'--build-path',$buildPath)
if ($ConfigFile) { $cliArguments += @('--config-file',$ConfigFile) }
$cliArguments += $firmwareRoot
Write-Host "Compiling $Board with $Jobs jobs. Log: $logPath"
Write-Host 'Persistent build directory is reused. First build and dependency discovery can take longer.'
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$ErrorActionPreference = 'Continue'
& $ArduinoCli @cliArguments 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding UTF8 -LiteralPath $logPath
$buildExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
@{elapsed_seconds=$watch.Elapsed.TotalSeconds;exit_code=$buildExitCode;jobs=$Jobs;fqbn=$fqbn} | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath $timingPath
Get-Content -LiteralPath $logPath -Tail 15
if ($buildExitCode -ne 0) { throw "Firmware build failed: $buildExitCode" }
Write-Host "Build completed in $([math]::Round($watch.Elapsed.TotalSeconds,1)) seconds. Images/ELF/MAP: $buildPath"
