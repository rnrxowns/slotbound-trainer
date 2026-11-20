# VS Build Tools에 포함된 CMake를 먼저 찾고, 없으면 PATH에서 찾습니다.
# 사용법  .\build.ps1          # Release, 저장소의 config/trainer.ini 사용
#       .\build.ps1 -Debug
#       .\build.ps1 -Dist      # DLL을 포함한 단일 EXE, 실행 파일과 같은 폴더에 설정 저장
param([switch]$Debug, [switch]$Dist)

$ErrorActionPreference = 'Stop'
$cmake = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $candidate = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $candidate) { $cmake = $candidate }
    }
}
if (-not $cmake) {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $onPath) { throw 'CMake를 찾을 수 없습니다. C++ CMake 도구가 포함된 VS Build Tools를 설치하거나 CMake를 PATH에 추가하세요.' }
    $cmake = $onPath.Source
}

$config = if ($Debug -and -not $Dist) { 'Debug' } else { 'Release' }
$buildDir = if ($Dist) { "$PSScriptRoot\build-dist" } else { "$PSScriptRoot\build" }
$portable = if ($Dist) { 'ON' } else { 'OFF' }
$exeName = 'slotbound_trainer_ko.exe'

& $cmake -S $PSScriptRoot -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DSB_PORTABLE=$portable"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
# 배포용 빌드에서는 개발용 진단 도구를 제외합니다.
$target = if ($Dist) { @('--target', 'slotbound_trainer') } else { @() }
& $cmake --build $buildDir --config $config @target
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if (-not $Dist) {
    Write-Host "`n빌드 완료  $buildDir\$config\$exeName (같은 폴더에 개발 도구와 DLL 포함)"
    exit 0
}

# 이전 배포 파일을 정리하되 사용자의 trainer.ini는 보존합니다.
$distDir = "$PSScriptRoot\dist"
New-Item -ItemType Directory -Force $distDir | Out-Null
Get-ChildItem $distDir -File | Where-Object { $_.Name -ne 'trainer.ini' } | Remove-Item -Force
Copy-Item "$buildDir\$config\$exeName" $distDir -Force
Write-Host "`n빌드 완료  $distDir\$exeName (설정을 바꾸면 같은 폴더에 trainer.ini가 생성됩니다.)"
