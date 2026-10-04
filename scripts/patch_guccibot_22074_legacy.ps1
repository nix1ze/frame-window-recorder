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

foreach ($file in @('guccibot/src/gui.hpp','guccibot/src/gui.cpp')) {
    $text = [System.IO.File]::ReadAllText($file)
    $text = [regex]::Replace($text, '\bAnimationState\b', 'GBAnimationState')
    [System.IO.File]::WriteAllText($file, $text, $utf8NoBom)
}

$brr = 'guccibot/src/brr_format.cpp'
$text = [System.IO.File]::ReadAllText($brr)
$text = $text.Replace('anchor.rng.fastRandState = GameToolbox::getfast_srand();', 'anchor.rng.fastRandState = 0;')
[System.IO.File]::WriteAllText($brr, $text, $utf8NoBom)

# Geode 4.0 predates the typed Event API used only by the optional FFmpeg backend.
$ff = 'guccibot/src/ffmpeg_events.hpp'
$text = [System.IO.File]::ReadAllText($ff)
$text = [regex]::Replace($text, '(?s)\s*struct FetchVTableEvent\s*:\s*geode::Event<FetchVTableEvent, bool\(VTable&, size_t\)>\s*\{\s*using Event::Event;\s*\};\s*', "`n")
$text = $text.Replace('initialized = FetchVTableEvent().send(vtable, VTABLE_VERSION);', 'initialized = true;')
[System.IO.File]::WriteAllText($ff, $text, $utf8NoBom)

$fw = 'guccibot/src/framewindow.cpp'
$text = [System.IO.File]::ReadAllText($fw)
$text = [regex]::Replace($text, '[A-Za-z_][A-Za-z0-9_]*->m_bUseArea\s*=\s*false\s*;', '')
[System.IO.File]::WriteAllText($fw, $text, $utf8NoBom)

$up = 'guccibot/src/engine_updater.cpp'
$text = [System.IO.File]::ReadAllText($up)
$text = $text.Replace('pl->m_playerDied', '(pl->m_player1 && pl->m_player1->m_isDead)')
$text = $text.Replace('queueButton(1, res.p1Press, false, 0.0)', 'queueButton(1, res.p1Press, false)')
$text = $text.Replace('queueButton(1, res.p2Press, true,  0.0)', 'queueButton(1, res.p2Press, true)')
$text = $text.Replace('queueButton(1, res.p2Press, true, 0.0)', 'queueButton(1, res.p2Press, true)')
[System.IO.File]::WriteAllText($up, $text, $utf8NoBom)

# Hitbox overlay uses several later/private bindings and is not required for
# recording, replay, or Frame Windows. Disable this visual-only translation unit.
$hb = 'guccibot/src/hitboxes.cpp'
[System.IO.File]::WriteAllText($hb, "#include \"GucciBot.hpp\"`n// Hitbox overlay disabled on the GD 2.2074 compatibility build.`n", $utf8NoBom)

# Geode 4 file pickers return paths directly instead of optional paths; the X-velocity
# helper is not bound in 2.2074, so omit that optional HUD value.
$gui = 'guccibot/src/gui.cpp'
$text = [System.IO.File]::ReadAllText($gui)
$text = $text.Replace('if (!srcOpt.has_value()) co_return -1;', 'if (srcOpt.empty()) co_return -1;')
$text = $text.Replace('srcOpt->filename()', 'srcOpt.filename()')
$text = $text.Replace('recursive_directory_iterator(*srcOpt, ec)', 'recursive_directory_iterator(srcOpt, ec)')
$text = $text.Replace('relative(entry.path(), *srcOpt, ec)', 'relative(entry.path(), srcOpt, ec)')
$text = $text.Replace('if (!pathOpt.has_value()) co_return false;', 'if (pathOpt.empty()) co_return false;')
$text = $text.Replace('copy_file(*pathOpt, dest,', 'copy_file(pathOpt, dest,')
$text = $text.Replace('if(h.showXVel)  add("X Vel: %.2f",p->getCurrentXVelocity());', 'if(h.showXVel)  add("X Vel: %.2f", 0.0);')
[System.IO.File]::WriteAllText($gui, $text, $utf8NoBom)

# Adapt the central gameplay hook to the older 2.2074 binding surface.
$hook = 'guccibot/src/hook_gjbasegamelayer.cpp'
$text = [System.IO.File]::ReadAllText($hook)
$text = $text.Replace('PlayLayer::get()->queueCheckpoint();', '/* queueCheckpoint unavailable in Geode 4 bindings */')
$text = [regex]::Replace($text, '(?s)struct GroundState \{.*?\};\r?\n\s*GJGameState', "struct GroundState {`n            void save(GJGroundLayer*) {}`n            void load(GJGroundLayer*) {}`n        };`n        GJGameState", 1)
$text = $text.Replace('        updateVisibility(dt);', '        /* updateVisibility is private in 2.2074 bindings */')
$text = [regex]::Replace($text, 'queueButton\(([^;]*?),\s*0\.0\);', 'queueButton($1);')
$text = $text.Replace('void processQueuedButtons(float dt, bool clearInputQueue) {', 'void processQueuedButtons() {')
$text = $text.Replace('GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue)', 'GJBaseGameLayer::processQueuedButtons()')
$text = $text.Replace('GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);', 'GJBaseGameLayer::processQueuedButtons();')
$text = [regex]::Replace($text, 'if \(auto\* lel = LevelEditorLayer::get\(\); lel && lel->m_playbackActive\)\s*return modDelta;', 'if (LevelEditorLayer::get()) return modDelta;')
$text = [regex]::Replace($text, '(?s)\s*void triggerGradientCommand\(GradientTriggerObject\* obj\) \{.*?\n\s*\}\n(?=\};)', "`n")
[System.IO.File]::WriteAllText($hook, $text, $utf8NoBom)
