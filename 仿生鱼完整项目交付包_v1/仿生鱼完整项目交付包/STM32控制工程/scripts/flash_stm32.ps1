[CmdletBinding()]
param(
    [string]$Firmware,
    [string]$Programmer,
    [ValidateSet('SWD', 'JTAG')][string]$Interface = 'SWD',
    [switch]$NoReset,
    [switch]$UnderReset
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Firmware) {
    $Firmware = @(
        (Join-Path $projectRoot 'build-firmware\bionic_fish.bin'),
        (Join-Path $projectRoot 'build-firmware\bionic_fish.elf'),
        (Join-Path $projectRoot 'build-firmware\bionic_fish.hex')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $Firmware -or -not (Test-Path -LiteralPath $Firmware -PathType Leaf)) {
    throw 'Firmware file not found. Build first or pass -Firmware <.bin/.hex/.elf>.'
}
$Firmware = (Resolve-Path -LiteralPath $Firmware).Path
$extension = [IO.Path]::GetExtension($Firmware).ToLowerInvariant()
if ($extension -notin '.bin', '.hex', '.elf') {
    throw 'Unsupported firmware format. Use .bin, .hex, or .elf.'
}

if (-not $Programmer) {
    $Programmer = (Get-Command 'STM32_Programmer_CLI.exe' -ErrorAction SilentlyContinue).Source
}
if (-not $Programmer) {
    $programFiles = [Environment]::GetFolderPath('ProgramFiles')
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $Programmer = @(
        (Join-Path $programFiles 'STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'),
        (Join-Path $programFilesX86 'STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'),
        'C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $Programmer -or -not (Test-Path -LiteralPath $Programmer -PathType Leaf)) {
    throw 'STM32_Programmer_CLI.exe not found. Install STM32CubeProgrammer or pass -Programmer <full path>.'
}
$Programmer = (Resolve-Path -LiteralPath $Programmer).Path

# Normal software reset works without an NRST wire. UnderReset requires NRST.
$connectionMode = if ($UnderReset) { 'UR' } else { 'NORMAL' }
$resetMode = if ($UnderReset) { 'HWrst' } else { 'SWrst' }
$programmerArguments = @('-c', "port=$Interface", "mode=$connectionMode", 'freq=4000', "reset=$resetMode", '-w', $Firmware)
if ($extension -eq '.bin') {
    $programmerArguments += '0x08000000'
}
# ELF and HEX already contain their load addresses.
$programmerArguments += '-v'
if (-not $NoReset) {
    $programmerArguments += '-rst'
}

Write-Host "Target STM32F103C8T6; firmware: $Firmware"
& $Programmer @programmerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Programming failed (exit code $LASTEXITCODE). Check ST-LINK, wiring, and target power."
}
Write-Host 'Programming and verification succeeded.' -ForegroundColor Green
