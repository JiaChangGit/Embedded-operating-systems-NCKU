param(
  [string]$Configuration = "Debug",
  [string]$OutputDirectory = "build-cli"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$gccCommand = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
if ($null -eq $gccCommand) {
  throw "arm-none-eabi-gcc was not found in PATH. Install GNU Arm Embedded Toolchain or build with STM32CubeIDE."
}

$gcc = $gccCommand.Source
$binDirectory = Split-Path $gcc
$sizeTool = Join-Path $binDirectory "arm-none-eabi-size.exe"
$buildDirectory = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (($buildDirectory -eq $projectRoot) -or
    (-not $buildDirectory.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar,
                                    [StringComparison]::OrdinalIgnoreCase))) {
  throw "OutputDirectory must stay inside the project workspace."
}
if (Test-Path $buildDirectory) {
  Remove-Item -LiteralPath $buildDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $buildDirectory | Out-Null

$includeDirectories = @(
  "Core/Inc",
  "Drivers/STM32F4xx_HAL_Driver/Inc",
  "Drivers/STM32F4xx_HAL_Driver/Inc/Legacy",
  "Drivers/CMSIS/Device/ST/STM32F4xx/Include",
  "Drivers/CMSIS/Include",
  "FATFS/Target",
  "FATFS/App",
  "USB_HOST/App",
  "USB_HOST/Target",
  "Middlewares/Third_Party/FatFs/src",
  "Middlewares/ST/STM32_USB_Host_Library/Core/Inc",
  "Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc",
  "Middlewares/Third_Party/FreeRTOS/Source/include",
  "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F"
)
$includeArguments = @()
foreach ($directory in $includeDirectories) {
  $includeArguments += "-I" + (Join-Path $projectRoot $directory)
}

$sourceDirectories = @(
  "Core/Src",
  "Drivers/STM32F4xx_HAL_Driver/Src",
  "FATFS/App",
  "FATFS/Target",
  "USB_HOST/App",
  "USB_HOST/Target",
  "Middlewares/ST/STM32_USB_Host_Library/Core/Src",
  "Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src",
  "Middlewares/Third_Party/FatFs/src",
  "Middlewares/Third_Party/FatFs/src/option",
  "Middlewares/Third_Party/FreeRTOS/Source",
  "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
  "Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang"
)

$optimization = if ($Configuration -eq "Release") { "-O2" } else { "-O0" }
$commonArguments = @(
  "-mcpu=cortex-m4",
  "-mthumb",
  "-mfpu=fpv4-sp-d16",
  "-mfloat-abi=hard",
  "-std=gnu11",
  $optimization,
  "-ffunction-sections",
  "-fdata-sections",
  "-Wall",
  "-DUSE_HAL_DRIVER",
  "-DSTM32F407xx",
  "--specs=nano.specs"
)
if ($Configuration -ne "Release") {
  $commonArguments += "-g3"
  $commonArguments += "-DDEBUG"
}

$objects = @()
foreach ($directory in $sourceDirectories) {
  foreach ($source in Get-ChildItem (Join-Path $projectRoot $directory) -File -Filter *.c) {
    $relativePath = $source.FullName.Substring($projectRoot.Length + 1)
    $objectName = ($relativePath -replace "[\\/:]", "_") -replace "\.c$", ".o"
    $objectPath = Join-Path $buildDirectory $objectName
    & $gcc @commonArguments @includeArguments -c $source.FullName -o $objectPath
    if ($LASTEXITCODE -ne 0) {
      throw "Compile failed: $relativePath"
    }
    $objects += $objectPath
  }
}

$startupSource = Join-Path $projectRoot "Core/Startup/startup_stm32f407vgtx.s"
$startupObject = Join-Path $buildDirectory "startup_stm32f407vgtx.o"
& $gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard `
  -x assembler-with-cpp -c $startupSource -o $startupObject
if ($LASTEXITCODE -ne 0) {
  throw "Startup assembly compile failed."
}
$objects += $startupObject

$elfPath = Join-Path $buildDirectory "USB_MP3.elf"
$mapPath = Join-Path $buildDirectory "USB_MP3.map"
$linkerScript = Join-Path $projectRoot "STM32F407VGTX_FLASH.ld"
& $gcc -o $elfPath @objects -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 `
  -mfloat-abi=hard "-T$linkerScript" --specs=nosys.specs --specs=nano.specs `
  "-Wl,--gc-sections" "-Wl,-Map=$mapPath" "-Wl,--start-group" -lc -lm `
  "-Wl,--end-group"
if ($LASTEXITCODE -ne 0) {
  throw "Link failed."
}

& $sizeTool $elfPath
Write-Host "Build completed: $elfPath"
