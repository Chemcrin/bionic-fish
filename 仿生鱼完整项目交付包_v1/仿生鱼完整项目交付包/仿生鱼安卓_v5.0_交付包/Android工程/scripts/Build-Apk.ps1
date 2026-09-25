param(
    [Parameter(Mandatory=$true)][string]$JdkHome,
    [Parameter(Mandatory=$true)][string]$AndroidSdk
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$artifactDirectory = Join-Path $projectDirectory 'artifacts'
$oldJavaHome = $env:JAVA_HOME
$oldAndroidHome = $env:ANDROID_HOME
$oldSdkRoot = $env:ANDROID_SDK_ROOT
try {
    $env:JAVA_HOME = (Resolve-Path -LiteralPath $JdkHome).Path
    $env:ANDROID_HOME = (Resolve-Path -LiteralPath $AndroidSdk).Path
    $env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
    New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
    # 先跑单测，失败即停止；不通过改协议或跳过测试来出包。
    & (Join-Path $projectDirectory 'gradlew.bat') -p $projectDirectory `
        :app:testDebugUnitTest --no-daemon --no-parallel --console=plain 2>&1 |
        Tee-Object -FilePath (Join-Path $artifactDirectory 'unit-tests-build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed; APK not delivered.' }
    & (Join-Path $projectDirectory 'gradlew.bat') -p $projectDirectory `
        :app:lintDebug :app:assembleDebug :app:assembleDebugAndroidTest `
        --no-daemon --no-parallel --console=plain 2>&1 |
        Tee-Object -FilePath (Join-Path $artifactDirectory 'apk-build.log')
    if ($LASTEXITCODE -ne 0) { throw 'APK build/lint failed.' }
    $apkFile = Join-Path $projectDirectory 'app/build/outputs/apk/debug/app-debug.apk'
    $toolsDirectory = Join-Path $env:ANDROID_HOME 'build-tools/36.0.0'
    & (Join-Path $toolsDirectory 'apksigner.bat') verify --verbose --print-certs $apkFile 2>&1 |
        Tee-Object -FilePath (Join-Path $artifactDirectory 'signature-verification.log')
    if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
    & (Join-Path $toolsDirectory 'zipalign.exe') -c 4 $apkFile
    if ($LASTEXITCODE -ne 0) { throw 'APK zip alignment failed.' }
    Copy-Item -LiteralPath $apkFile -Destination (Join-Path $artifactDirectory 'BionicFish-v0.5.0.apk') -Force
    Get-FileHash -LiteralPath $apkFile -Algorithm SHA256
} finally {
    $env:JAVA_HOME = $oldJavaHome
    $env:ANDROID_HOME = $oldAndroidHome
    $env:ANDROID_SDK_ROOT = $oldSdkRoot
}
