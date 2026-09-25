<#
  仿生鱼 STM32 固件：编译 + 烧录（一条命令，离线，复用本机已有工具链）

  用法（在任意目录执行）：
    powershell -File build_and_flash_bionic_fish.ps1              # 编译当前源码并烧录
    powershell -File build_and_flash_bionic_fish.ps1 -SkipBuild   # 只烧录已有 BIN
    powershell -File build_and_flash_bionic_fish.ps1 -SkipFlash   # 只编译
    powershell -File build_and_flash_bionic_fish.ps1 -FirmwareElf # 烧 ELF 而不是 BIN
    powershell -File build_and_flash_bionic_fish.ps1 -UnderReset  # 需要 NRST 接线的连接方式

  依赖（本机已就位，均无需联网）：
    CubeF1 v1.8.6   C:\Users\CloudyRain\STM32Cube\Repository\STM32Cube_FW_F1_V1.8.6
    arm-none-eabi   CubeIDE 13.3.rel1（默认路径见 $ArmBin）
    cmake / ninja   MATLAB 3.25.0 / CLion ninja
    STM32_Programmer_CLI  CubeIDE 插件自带（默认路径见 $Programmer）
  缺失时用对应参数显式覆盖，例如 -CMake D:\CLion 2026.1\bin\cmake\win\x64\bin\cmake.exe
#>
[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [string]$CubePath    = 'C:\Users\CloudyRain\STM32Cube\Repository\STM32Cube_FW_F1_V1.8.6',
  [string]$ArmBin      = 'D:\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin',
  [string]$CMake       = 'D:\MATLAB\R2024a\bin\win64\cmake\bin\cmake.exe',
  [string]$Ninja       = 'D:\CLion 2026.1\bin\ninja\win\x64\ninja.exe',
  [string]$Programmer  = 'D:\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.200.202503041107\tools\bin\STM32_Programmer_CLI.exe',
  [string]$Python      = 'D:\Python311\python.exe',
  [switch]$SkipBuild,
  [switch]$SkipFlash,
  [switch]$FirmwareElf,
  [switch]$UnderReset
)

$ErrorActionPreference = 'Stop'

if (-not $ProjectRoot) {
  # 默认取仓库内 v1 交付包的固件工程（脚本置于仓库根目录即可直接运行，无需改路径）
  $ProjectRoot = Join-Path $PSScriptRoot '仿生鱼完整项目交付包_v1\仿生鱼完整项目交付包\STM32控制工程'
}

function Assert-Path([string]$Path, [string]$What) {
  if (-not (Test-Path -LiteralPath $Path)) { throw "$What not found: $Path" }
}

# ---------- 编译 ----------
if (-not $SkipBuild) {
  foreach ($pair in @(
      @($Python,     'Python'),
      @($CMake,      'CMake'),
      @($Ninja,      'Ninja'),
      @($CubePath,   'STM32CubeF1 v1.8.6'),
      @((Join-Path $ArmBin 'arm-none-eabi-gcc.exe'), 'arm-none-eabi-gcc'))) {
    Assert-Path $pair[0] $pair[1]
  }
  Write-Host '== 编译固件 (CMake + Ninja + arm-none-eabi-gcc, WERROR=ON) ==' -ForegroundColor Cyan
  Push-Location $ProjectRoot
  try {
    & $Python (Join-Path $ProjectRoot 'scripts\build.py') firmware `
        --cmake $CMake --ninja $Ninja --cube-path $CubePath --toolchain-bin $ArmBin
    if ($LASTEXITCODE -ne 0) { throw "构建失败 (exit $LASTEXITCODE)" }
  } finally { Pop-Location }
}

$buildDir = Join-Path $ProjectRoot 'build-firmware'
$bin = Join-Path $buildDir 'bionic_fish.bin'
$elf = Join-Path $buildDir 'bionic_fish.elf'
if ((Test-Path -LiteralPath $elf) -and -not (Test-Path -LiteralPath $bin)) {
  # bin 由 POST_BUILD 生成；只有在 bin 缺失时才回退
  $FirmwareElf = $true
}
$firmware = if ($FirmwareElf) { $elf } else { $bin }
Assert-Path $firmware '固件文件'

if (-not $SkipBuild) {
  $info = Get-Item -LiteralPath $bin
  $sha  = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash.ToLower()
  $head = (& git -C $ProjectRoot rev-parse --short HEAD 2>$null)
  Write-Host ("== 产物: {0}  {1} 字节  sha256={2}  源提交={3}" -f $info.Name, $info.Length, $sha.Substring(0, 16), $head) -ForegroundColor Cyan
}

# ---------- 烧录 ----------
if (-not $SkipFlash) {
  Assert-Path $Programmer 'STM32_Programmer_CLI'
  Write-Host '== 烧录 (ST-Link / SWD) ==' -ForegroundColor Cyan
  $flashArgs = @{
    Firmware   = $firmware
    Programmer = $Programmer
    Interface  = 'SWD'
  }
  if ($UnderReset) { $flashArgs['UnderReset'] = $true }
  & (Join-Path $ProjectRoot 'scripts\flash_stm32.ps1') @flashArgs
  if ($LASTEXITCODE -ne 0) { throw "烧录失败 (exit $LASTEXITCODE)" }
  Write-Host '== 完成：已写入并校验，MCU 已复位运行 ==' -ForegroundColor Green
}
