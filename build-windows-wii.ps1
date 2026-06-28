[CmdletBinding()]
param(
    [string]$BuildDir = "build\wii",
    [string]$Configuration = "Release",
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

$cmakeExe = "C:\devkitPro\msys2\usr\bin\cmake.exe"
$msysBin = "C:\devkitPro\msys2\usr\bin"
$toolchainFile = "/opt/devkitpro/cmake/Wii.cmake"

try {
    if (-not (Test-Path $cmakeExe)) {
        throw "devkitPro CMake was not found at '$cmakeExe'. Install devkitPro with the MSYS2 tools first."
    }

    $env:PATH = "$msysBin;$env:PATH"

    & $cmakeExe --fresh -S . -B $BuildDir -G "Unix Makefiles" `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" `
        -DPLATFORM=wii `
        "-DCMAKE_BUILD_TYPE=$Configuration"
    if ($LASTEXITCODE -ne 0) {
        throw "Wii CMake configure failed."
    }

    & $cmakeExe --build $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Wii build failed."
    }

    Write-Host ""
    Write-Host "Wii build completed successfully." -ForegroundColor Green
}
catch {
    Write-Host ""
    Write-Host "Wii build failed." -ForegroundColor Red
    Write-Host $_
    exit 1
}
finally {
    if (-not $NoPause) {
        Write-Host ""
        Read-Host "Press Enter to close"
    }
}
