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

# Geode 4 exposes cocos2d::extension::AnimationState in the precompiled headers,
# which collides with GucciBot's own UI helper type. Rename GucciBot's type only.
foreach ($file in @('guccibot/src/gui.hpp','guccibot/src/gui.cpp')) {
    $text = [System.IO.File]::ReadAllText($file)
    $text = [regex]::Replace($text, '\bAnimationState\b', 'GBAnimationState')
    [System.IO.File]::WriteAllText($file, $text, $utf8NoBom)
}

# getfast_srand is private/unimplemented in the 2.2074 bindings. Preserve the
# anchor structure but leave that optional snapshot unset on this compatibility build.
$brr = 'guccibot/src/brr_format.cpp'
$text = [System.IO.File]::ReadAllText($brr)
$text = $text.Replace('anchor.rng.fastRandState = GameToolbox::getfast_srand();', 'anchor.rng.fastRandState = 0;')
[System.IO.File]::WriteAllText($brr, $text, $utf8NoBom)
