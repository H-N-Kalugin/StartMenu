# Переносит группы и плитки Start11 в items.json нашего меню.
# Start11 только читается (реестр HKCU\Software\Stardock\Start8\Start8.ini), ничего в нём не меняется.
# Запуск: pwsh -File tools\import-start11.ps1   (текущий items.json сохраняется рядом как items.json.bak)

$ErrorActionPreference = 'Stop'
$base = 'HKCU:\Software\Stardock\Start8\Start8.ini'
$target = Join-Path $env:APPDATA 'StartMenu\items.json'

function Get-Values($path) {
    $p = Get-ItemProperty -LiteralPath $path -ErrorAction SilentlyContinue
    if (-not $p) { return @() }
    $p.PSObject.Properties | Where-Object { $_.Name -match '^\d+$' } |
        Sort-Object { [int]$_.Name } | ForEach-Object { [string]$_.Value }
}

$sizeNames = @{ 0 = 'small'; 1 = 'medium'; 2 = 'wide'; 3 = 'large' }
$sizeCells = @{ 0 = 1; 1 = 2; 2 = 4; 3 = 4 }

# Группы Start11: id|название|…|колонка(7)|…|вкладочная группа(10)|цвет фона(11)|…
$raw = foreach ($line in Get-Values "$base\Groups") {
    $f = $line -split '\|'
    $tiles = foreach ($t in Get-Values "$base\GroupContents\$($f[0])") {
        $tf = $t -split '\|'
        $path = [Environment]::ExpandEnvironmentVariables($tf[0])
        [pscustomobject]@{
            name = [IO.Path]::GetFileNameWithoutExtension($path)
            path = $path -replace '\\', '/'
            size = [int]$tf[2]; x = [int]$tf[3]; y = [int]$tf[4]
            exists = Test-Path -LiteralPath $path
        }
    }
    $tiles = @($tiles | Sort-Object y, x)
    $need = 0
    foreach ($t in $tiles) { $need = [Math]::Max($need, $t.x + $sizeCells[$t.size]) }
    # Цвет фона группы у Start11 — число 0xAABBGGRR (как COLORREF, но с прозрачностью). 0 — без фона.
    $c = [int64]$f[11]
    $color = $null
    if ($c -ne 0) {
        $u = [uint32]($c -band 0xFFFFFFFF)
        $color = '#{0:x2}{1:x2}{2:x2}{3:x2}' -f (($u -shr 24) -band 0xFF), ($u -band 0xFF), (($u -shr 8) -band 0xFF), (($u -shr 16) -band 0xFF)
    }
    [pscustomobject]@{
        title = $f[1]; column = [Math]::Max(0, [int]$f[7] - 1); tabGroup = $f[10]
        clean = ($c -eq 0); color = $color; need = $need; tiles = $tiles
    }
}

# Ширина группы = самая широкая группа в её колонке (так выглядит в Start11), но не меньше 4 ячеек.
$colWidth = @{}
foreach ($g in $raw) { $colWidth[$g.column] = [Math]::Max([int]$colWidth[$g.column], [Math]::Max(4, $g.need)) }

# Группы с общей «вкладочной группой» сливаются в одну группу с вкладками.
$groups = [Collections.Generic.List[object]]::new()
$byTab = @{}
foreach ($g in $raw) {
    $tab = [ordered]@{
        title = $g.title
        tiles = @($g.tiles | ForEach-Object { [ordered]@{ name = $_.name; path = $_.path; size = $sizeNames[$_.size] } })
    }
    if ($g.tabGroup -and $byTab.ContainsKey($g.tabGroup)) {
        $byTab[$g.tabGroup].tabs += $tab
        continue
    }
    $entry = [ordered]@{ tabs = @($tab); activeTab = 0; clean = $g.clean; width = $colWidth[$g.column]; column = $g.column }
    if ($g.color) { $entry.color = $g.color }
    if ($g.tabGroup) { $byTab[$g.tabGroup] = $entry }
    $groups.Add($entry)
}

# Что уже было закреплено в нашем меню и чего нет в Start11 — оставляем отдельной группой.
$sidebar = $null
if (Test-Path $target) {
    Copy-Item $target "$target.bak" -Force
    $old = Get-Content $target -Raw -Encoding utf8 | ConvertFrom-Json
    $sidebar = $old.sidebar
    $known = @($groups | ForEach-Object { $_.tabs } | ForEach-Object { $_.tiles } | ForEach-Object { $_.path.ToLower() })
    $extra = @()
    $oldTiles = @()
    foreach ($og in @($old.groups)) {
        $tabs = if ($og.tabs) { $og.tabs } else { @($og) }
        foreach ($ot in $tabs) { $oldTiles += @($ot.tiles) }
    }
    $oldTiles += @($old.items)   # формат первой версии: плоский список "items"
    foreach ($t in $oldTiles) {
        if ($t -and $t.path -and $known -notcontains $t.path.ToLower()) {
            $extra += [ordered]@{ name = $t.name; path = $t.path; size = $(if ($t.size) { $t.size } else { 'medium' }) }
        }
    }
    if ($extra.Count) {
        $groups.Add([ordered]@{ tabs = @([ordered]@{ title = 'Закреплённые'; tiles = $extra }); activeTab = 0; clean = $false; width = 8; column = 0 })
    }
}

$doc = [ordered]@{ version = 2; groups = $groups }
if ($sidebar) { $doc.sidebar = $sidebar }
New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
$doc | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $target -Encoding utf8

# Отчёт
foreach ($g in $groups) {
    foreach ($t in $g.tabs) {
        "[{0}] колонка {1}, ширина {2}{3}" -f $t.title, $g.column, $g.width, $(if ($g.clean) { ', без фона' } else { '' })
        foreach ($tile in $t.tiles) {
            $ok = if (Test-Path -LiteralPath $tile.path) { '' } else { '   <- ФАЙЛ НЕ НАЙДЕН' }
            "    {0} ({1}){2}" -f $tile.name, $tile.size, $ok
        }
    }
}
"Записано: $target"
