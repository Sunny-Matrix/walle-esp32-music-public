$ErrorActionPreference = "Stop"

$idfPath = "D:\esp\.espressif\v5.4.4\esp-idf"
$toolsPath = "C:\Espressif\tools"
$python = "$toolsPath\python\v5.4.4\venv\Scripts\python.exe"
$cmakePath = "$toolsPath\cmake\3.30.2\bin"
$ninjaPath = "$toolsPath\ninja\1.12.1"
$ninja = "$ninjaPath\ninja.exe"
$toolchainPath = "$toolsPath\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$buildPath = "build-robot-s3-v544"

$requiredPaths = @(
    $idfPath,
    $python,
    "$cmakePath\cmake.exe",
    $ninja,
    "$toolchainPath\xtensa-esp-elf-gcc.exe"
)

foreach ($path in $requiredPaths) {
    if (-not (Test-Path $path)) {
        throw "Required ESP-IDF file is missing: $path"
    }
}

$env:IDF_PATH = $idfPath
$env:IDF_TOOLS_PATH = $toolsPath
$env:IDF_PYTHON_ENV_PATH = "$toolsPath\python\v5.4.4\venv"
$env:IDF_TARGET = "esp32s3"
$env:Path = "$cmakePath;$ninjaPath;$toolchainPath;$env:Path"
$env:ESP_ROM_ELF_DIR = "$toolsPath\esp-rom-elfs\20241011"

Write-Host "[1/2] Configuring ESP32-S3 project..." -ForegroundColor Cyan
# The project already targets ESP32-S3. Reconfigure preserves sdkconfig and
# cached components; set-target would force a full clean every time.
& $python "$idfPath\tools\idf.py" -B $buildPath reconfigure
if ($LASTEXITCODE -ne 0) {
    throw "Failed to configure the ESP32-S3 target."
}

$requiredConfig = @(
    "CONFIG_BOARD_TYPE_ROBOT_S3=y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
    "CONFIG_PARTITION_TABLE_CUSTOM=y",
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v1/16m.csv"',
    "CONFIG_LV_FONT_MONTSERRAT_30=y"
)

$sdkconfig = Join-Path $PSScriptRoot "sdkconfig"
$currentConfig = Get-Content $sdkconfig -Raw
foreach ($line in $requiredConfig) {
    if (-not $currentConfig.Contains($line)) {
        Add-Content -Path $sdkconfig -Value $line
    }
}

Write-Host "[2/2] Building robot-s3 firmware..." -ForegroundColor Cyan
& $python "$idfPath\tools\idf.py" -B $buildPath -DBOARD_NAME=robot-s3 build
if ($LASTEXITCODE -ne 0) {
    throw "Firmware build failed. Nothing was flashed."
}

Write-Host "Build succeeded. Nothing was flashed." -ForegroundColor Green
