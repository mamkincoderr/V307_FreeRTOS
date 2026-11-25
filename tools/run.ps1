<#
 Драйвер проекта V307_FreeRTOS (из H417_FreeRTOS). Все Build Targets MounRiver Studio и все сценарии из терминала идут через него.
 Автор: Дмитрий (GitHub: mamkincoderr, https://github.com/mamkincoderr)
 Telegram: https://t.me/oDeXteRo

 Запуск: powershell -ExecutionPolicy Bypass -File tools\run.ps1 <команда>

 Команды
   build   настроить CMake (если нужно) и собрать obj\V307_FreeRTOS.hex
   clean   удалить каталог сборки obj
   flash   собрать, записать obj\V307_FreeRTOS.hex, проверить, пустить
   reset   снять забытые точки останова, сбросить и пустить чип
   erase   стереть всю флеш
   info    CHIPID, объём флеша, UID, option bytes и режим памяти
   size    размеры секций образа

 OpenOCD и GCC — из MounRiver Studio 2. Другая установка: переменные WCH_OPENOCD_DIR и WCH_TOOLCHAIN_DIR.
 Первый пуск после смены режима памяти (option bytes) чип сбрасывается сам — это нормально.
#>
param([Parameter(Position = 0)][string]$Command = 'help')
$ErrorActionPreference = 'Stop'

# Консоль MRS читает системную кодировку (1251); без этого кириллица превращается в мусор.
if ([Console]::IsOutputRedirected) {
    try { [Console]::OutputEncoding = [System.Text.Encoding]::Default } catch { }
}

$Root     = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root 'obj'
$Image    = Join-Path $BuildDir 'V307_FreeRTOS.hex'
$Mrs2     = 'MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH'

function Find-Dir([string]$EnvName, [string]$Sub, [string]$Exe) {
    $cands = @()
    $v = [Environment]::GetEnvironmentVariable($EnvName)
    if ($v) { $cands += $v }
    foreach ($d in 'C', 'D', 'E', 'F') {
        foreach ($r in "${d}:\", "${d}:\Program Files\", "${d}:\Program Files (x86)\") { $cands += "$r$Mrs2\$Sub" }
    }
    foreach ($c in $cands) { if (Test-Path (Join-Path $c $Exe)) { return $c } }
    throw "$Exe не найден. Укажите папку в переменной $EnvName."
}

function Invoke-Ocd([string[]]$Cmds) {
    $dir = Find-Dir 'WCH_OPENOCD_DIR' 'OpenOCD\OpenOCD\bin' 'openocd.exe'
    $a = @('-f', (Join-Path $dir 'wch-riscv.cfg'), '-f', (Join-Path $PSScriptRoot 'v307.cfg'))
    foreach ($c in $Cmds) { $a += @('-c', $c) }
    $ocd = Join-Path $dir 'openocd.exe'
    # OpenOCD пишет в stderr; собираем весь вывод, чтобы в консоли MRS он не выглядел ошибкой.
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $log = & $ocd @a 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    # Без шума: «Unsupported register (8)» и «Failed to flush registers» безвредны, но консоль MRS
    # считает строки «Error:» ошибками сборки и пишет Build Failed после удачной прошивки.
    $log | Where-Object { $_ -notmatch 'Licensed under|bugs\.html|For bug reports|transport|gdb server|Listening on port|^Open On-Chip|Unsupported register|Failed to flush registers|RemoteException|^v307_|^\s*$' } |
        ForEach-Object { Write-Host $_ }
    if ($code -ne 0 -or ($log -match 'failed to connect')) {
        throw 'OpenOCD: нет связи с чипом. Проверьте WCH-Link и питание платы; если не помогает — RESET на плате и повтор.'
    }
    return $log
}

# tools\gcc и tools\openocd — ссылки (junction) на GCC 15 и OpenOCD из MRS2. Через них конфигурации отладки
# MRS 1.92 обходятся без абсолютных путей. Кроме того, путь, где есть «RISC-V Embedded GCC», MRS 1.92 портит
# (дописывает 12 по выбранному в .cproject компилятору).
function Initialize-ToolLink([string]$Name, [string]$Target) {
    $link = Join-Path $PSScriptRoot $Name
    if ((Test-Path $link) -and ((Get-Item $link).Target -contains $Target)) { return }
    if (Test-Path $link) { cmd /c rmdir "$link" | Out-Null }
    New-Item -ItemType Junction -Path $link -Target $Target | Out-Null
    Write-Host "tools\$Name -> $Target"
}

function Initialize-ToolLinks {
    $gccBin = Find-Dir 'WCH_TOOLCHAIN_DIR' 'Toolchain\RISC-V Embedded GCC15\bin' 'riscv32-wch-elf-gcc.exe'
    Initialize-ToolLink 'gcc' (Split-Path -Parent $gccBin)
    Initialize-ToolLink 'openocd' (Find-Dir 'WCH_OPENOCD_DIR' 'OpenOCD\OpenOCD\bin' 'openocd.exe')
}

function Do-Build {
    Initialize-ToolLinks
    $env:PATH = "$env:PATH;$env:ProgramFiles\CMake\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links"
    Push-Location $Root
    try {
        if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
            cmake --preset default
            if ($LASTEXITCODE -ne 0) { throw 'Не удалась настройка CMake' }
        }
        cmake --build --preset default
        if ($LASTEXITCODE -ne 0) { throw 'Сборка не удалась' }
    } finally { Pop-Location }
}

$Tcl = { param($p) '{' + $p.Replace('\', '/') + '}' }

switch ($Command) {
    'build' { Do-Build }

    'clean' {
        if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
        Write-Host "Удалено: $BuildDir"
    }

    'flash' {
        Do-Build
        $log = Invoke-Ocd @('init', "v307_flash $(& $Tcl $Image)", 'shutdown')
        if (-not ($log -match 'Verified OK')) { throw 'Прошивка не подтверждена (нет Verified OK)' }
        Write-Host 'Записано и проверено: V307_FreeRTOS.hex, чип запущен.'
    }

    'reset' { Invoke-Ocd @('init', 'v307_reset', 'shutdown') | Out-Null; Write-Host 'Точки останова сняты, чип сброшен и запущен.' }

    'erase' {
        Invoke-Ocd @('init', 'v307_clear_bp', 'wch_riscv unfreeze', 'flash erase_sector 0 0 last', 'wlink_reset_resume', 'shutdown') | Out-Null
        Write-Host 'Флеш стёрта.'
    }

    'info' {
        Invoke-Ocd @('init', 'halt', 'mdw 0x1FFFF704', 'mdw 0x1FFFF7E0', 'mdw 0x1FFFF7E8 3', 'mdw 0x1FFFF800 2', 'mdw 0x4002201C', 'wlink_reset_resume', 'shutdown') | Out-Null
        Write-Host 'CHIPID = 0x1FFFF704 (0x30700518 = CH32V307VCT6), флеш КБ = младшие 16 бит 0x1FFFF7E0, UID = 0x1FFFF7E8.'
        Write-Host 'Option bytes 0x1FFFF800; FLASH_OBR 0x4002201C: биты 9:8 = режим памяти (00 = 192K + 128K).'
    }

    'size' {
        $gcc = Find-Dir 'WCH_TOOLCHAIN_DIR' 'Toolchain\RISC-V Embedded GCC15\bin' 'riscv32-wch-elf-size.exe'
        & (Join-Path $gcc 'riscv32-wch-elf-size.exe') -A (Join-Path $BuildDir 'V307_FreeRTOS.elf')
    }

    default { Get-Help $PSCommandPath | Out-String | Write-Host }
}
