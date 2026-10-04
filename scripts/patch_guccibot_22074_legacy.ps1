$ErrorActionPreference = 'Stop'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$p = 'guccibot/mod.json'
$j = Get-Content $p -Raw | ConvertFrom-Json
$j.geode = '4.0.0'
$j.gd.win = '2.2074'
$j.version = 'v1.4.0-22074'
$j.description = $j.description + ' (GD 2.2074 compatibility build)'
[System.IO.File]::WriteAllText($p, ($j | ConvertTo-Json -Depth 20), $utf8NoBom)

$cmake = 'guccibot/CMakeLists.txt'
$src = Get-Content $cmake -Raw
$src = $src.Replace('CPMAddPackage("gh:cursey/safetyhook#main")', 'CPMAddPackage("gh:cursey/safetyhook#661227263199a15a046b241bb096cf4501e6023a")')
[System.IO.File]::WriteAllText($cmake, $src, $utf8NoBom)
