param([string]$Compiler = 'clang')
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectDir
try {
    $outputDir = Join-Path $env:TEMP 'croqchat-native-tests'
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $prefix = @()
    if ([IO.Path]::GetFileName($Compiler) -eq 'zig.exe') { $prefix = @('cc') }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/protocol tests/protocol_test.c src/protocol/ble_protocol.c -o "$outputDir/protocol.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests protocole' }
    & "$outputDir/protocol.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests protocole' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude -Isrc/fb05_scheduler -Isrc/fb06_ble -Isrc/fb01_weight -Isrc/fb02_motor -Isrc/fb03_dispenser -Isrc/fb04_safety -Isrc/cat_identification -Isrc/protocol tests/scheduler_test.c src/fb05_scheduler/meal_scheduler.c src/fb06_ble/ble_commands.c src/fb04_safety/safety_manager.c src/protocol/ble_protocol.c -o "$outputDir/scheduler.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests scheduler' }
    & "$outputDir/scheduler.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests scheduler' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude -Isrc/cat_identification -Isrc/fb02_motor -Isrc/fb01_weight tests/manual_badge_test.c src/cat_identification/manual_badge.c -o "$outputDir/badge.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests badge' }
    & "$outputDir/badge.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests badge' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude -Isrc/cat_identification tests/rfid_presence_test.c src/cat_identification/cat_identification.c -o "$outputDir/rfid.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests presence RFID' }
    & "$outputDir/rfid.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests presence RFID' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude -Isrc/common -Isrc/cat_identification -Isrc/fb01_weight -Isrc/fb02_motor -Isrc/fb03_dispenser -Isrc/fb04_safety -Isrc/fb05_scheduler -Isrc/fb06_ble -Isrc/protocol tests/app_controller_test.c src/common/app_controller.c src/cat_identification/manual_badge.c -o "$outputDir/coordinator.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests coordinateur' }
    & "$outputDir/coordinator.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests coordinateur' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Isrc/fb01_weight -Isrc/fb02_motor -Isrc/fb03_dispenser tests/dispenser_tare_test.c src/fb03_dispenser/dispenser_controller.c -o "$outputDir/dispenser.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests tare distributeur' }
    & "$outputDir/dispenser.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests tare distributeur' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude -Isrc/fb01_weight -Isrc/fb02_motor tests/weight_tare_test.c src/fb01_weight/weight_manager.c -o "$outputDir/weight.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests tare HX711' }
    & "$outputDir/weight.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests tare HX711' }
    & $Compiler @prefix -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude -Isrc/fb01_weight -Isrc/fb02_motor -Isrc/fb03_dispenser -Isrc/fb04_safety -Isrc/cat_identification tests/safety_timeout_test.c src/fb04_safety/safety_manager.c -o "$outputDir/timeout.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation tests timeout moteur' }
    & "$outputDir/timeout.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests timeout moteur' }
} finally { Pop-Location }
