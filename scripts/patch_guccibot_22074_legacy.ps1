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

# Geode 4.0 predates the typed Event API used only to discover the optional
# external FFmpeg recorder vtable. Leaving the vtable empty disables that
# optional backend while keeping the rest of GucciBot available.
$ff = 'guccibot/src/ffmpeg_events.hpp'
$text = [System.IO.File]::ReadAllText($ff)
$text = [regex]::Replace($text, '(?s)\s*struct FetchVTableEvent\s*:\s*geode::Event<FetchVTableEvent, bool\(VTable&, size_t\)>\s*\{\s*using Event::Event;\s*\};\s*', "`n")
$text = $text.Replace('initialized = FetchVTableEvent().send(vtable, VTABLE_VERSION);', 'initialized = true;')
[System.IO.File]::WriteAllText($ff, $text, $utf8NoBom)

# Old CCDrawNode keeps m_bUseArea protected.
$fw = 'guccibot/src/framewindow.cpp'
$text = [System.IO.File]::ReadAllText($fw)
$text = [regex]::Replace($text, '[A-Za-z_][A-Za-z0-9_]*->m_bUseArea\s*=\s*false\s*;', '')
[System.IO.File]::WriteAllText($fw, $text, $utf8NoBom)

# Small binding differences between 2.2074 and later versions.
$up = 'guccibot/src/engine_updater.cpp'
$text = [System.IO.File]::ReadAllText($up)
$text = $text.Replace('pl->m_playerDied', '(pl->m_player1 && pl->m_player1->m_isDead)')
$text = $text.Replace('queueButton(1, res.p1Press, false, 0.0)', 'queueButton(1, res.p1Press, false)')
$text = $text.Replace('queueButton(1, res.p2Press, true,  0.0)', 'queueButton(1, res.p2Press, true)')
$text = $text.Replace('queueButton(1, res.p2Press, true, 0.0)', 'queueButton(1, res.p2Press, true)')
[System.IO.File]::WriteAllText($up, $text, $utf8NoBom)
