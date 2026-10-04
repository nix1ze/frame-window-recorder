$ErrorActionPreference = 'Stop'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$p = 'guccibot/mod.json'
$j = Get-Content $p -Raw | ConvertFrom-Json
$j.geode = '4.0.0'
$j.gd.win = '2.2074'
$j.version = 'v2.0.0-beta.1-22074'
$j.description = $j.description + ' (GD 2.2074 compatibility build)'
[System.IO.File]::WriteAllText($p, ($j | ConvertTo-Json -Depth 20), $utf8NoBom)

$cmake = 'guccibot/CMakeLists.txt'
$src = Get-Content $cmake -Raw
$src = $src.Replace('CPMAddPackage("gh:cursey/safetyhook#main")', 'CPMAddPackage("gh:cursey/safetyhook#661227263199a15a046b241bb096cf4501e6023a")')
[System.IO.File]::WriteAllText($cmake, $src, $utf8NoBom)

Get-ChildItem 'guccibot/src' -Recurse -File | Where-Object { $_.Extension -in '.cpp','.hpp','.h' } | ForEach-Object {
    $text = [System.IO.File]::ReadAllText($_.FullName)
    $text = [regex]::Replace($text, '(?<!:)std::', '::std::')
    [System.IO.File]::WriteAllText($_.FullName, $text, $utf8NoBom)
}

# GD 2.2074 uses void-returning FMOD effect methods.
$audio = 'guccibot/src/audio/audio_hook.cpp'
$text = [System.IO.File]::ReadAllText($audio)
$text = $text.Replace('int playEffect(gd::string path) {', 'void playEffect(gd::string path) {')
$text = $text.Replace('int playEffect(gd::string path, float speed, float unknown, float volume) {', 'void playEffect(gd::string path, float speed, float unknown, float volume) {')
$text = $text.Replace('int playEffectAdvanced(gd::string path, float speed, float unknown, float volume,', 'void playEffectAdvanced(gd::string path, float speed, float unknown, float volume,')
$text = $text.Replace('return 0;', 'return;')
$text = [regex]::Replace($text, 'return FMODAudioEngine::playEffect\(([^;]+)\);', 'FMODAudioEngine::playEffect($1); return;')
$text = [regex]::Replace($text, 'return FMODAudioEngine::playEffectAdvanced\((?s:(.*?))\);', 'FMODAudioEngine::playEffectAdvanced($1); return;')
[System.IO.File]::WriteAllText($audio, $text, $utf8NoBom)

$fw = 'guccibot/src/analysis/ac/framewindow.cpp'
$text = [System.IO.File]::ReadAllText($fw)
$text = $text.Replace('m_probeDied || pl->m_playerDied', 'm_probeDied')
$text = $text.Replace('pl->m_playerDied = false;', '/* GD 2.2074: m_probeDied is authoritative */')
$text = $text.Replace('pl->m_playerDied', 'm_probeDied')
$diagPattern = '(?s)namespace \{\r?\nstruct PlayerField.*?\}\s*// namespace gucci\r?\n'
$diagStub = "namespace gucci {`n::std::vector<::std::string> fwPlayerFieldDump(PlayerObject* p) {`n    (void)p;`n    return {};`n}`n} // namespace gucci`n"
$text = [regex]::Replace($text, $diagPattern, $diagStub, 1)
$reportPattern = '(?s)void FrameWindowAnalyzer::reportStateDiff\(PlayLayer\* pl\) \{.*?\r?\n\}\r?\n\r?\n(?=void FrameWindowAnalyzer::reassertHeldButtons)'
$reportStub = "void FrameWindowAnalyzer::reportStateDiff(PlayLayer* pl) {`n    (void)pl;`n}`n`n"
$text = [regex]::Replace($text, $reportPattern, $reportStub, 1)
[System.IO.File]::WriteAllText($fw, $text, $utf8NoBom)

$mp = 'guccibot/src/analysis/macropath.cpp'
$text = [System.IO.File]::ReadAllText($mp)
$text = $text.Replace('lineNode->m_bUseArea = false;', '')
$text = $text.Replace('markerNode->m_bUseArea = false;', '')
[System.IO.File]::WriteAllText($mp, $text, $utf8NoBom)

$pf = 'guccibot/src/analysis/pathfinder.cpp'
$text = [System.IO.File]::ReadAllText($pf)
$text = $text.Replace('pl->m_playerDied', '(pl->m_player1 && pl->m_player1->m_isDead)')
[System.IO.File]::WriteAllText($pf, $text, $utf8NoBom)

# Trajectory preview depends on 2.2081-only player fields; keep API but disable the preview on 2.2074.
$traj = 'guccibot/src/analysis/trajectory.cpp'
$trajStub = @'
#include "trajectory.hpp"

namespace gucci {
TrajectoryPredictionService& TrajectoryPredictionService::get() {
    static TrajectoryPredictionService s;
    return s;
}
bool TrajectoryPredictionService::isActiveSimulation() const { return false; }
bool TrajectoryPredictionService::isProcessingOrbTouch() const { return false; }
void TrajectoryPredictionService::markDirty() { m_context.dirty = true; }
void TrajectoryPredictionService::clearOverlay() {}
void TrajectoryPredictionService::attach(PlayLayer*) {}
void TrajectoryPredictionService::detach() {}
void TrajectoryPredictionService::updatePreview(PlayLayer*) {}
bool TrajectoryPredictionService::probeAgency(PlayLayer*, PlayerObject*, AgencyResult& out, int) { out = {}; return false; }
bool TrajectoryPredictionService::predictStep(PlayLayer*, PlayerObject* source, bool, cocos2d::CCPoint& outPos, float& outRot) {
    if (source) { outPos = source->getPosition(); outRot = source->getRotation(); }
    return false;
}
bool TrajectoryPredictionService::extrapolateSubtick(PlayLayer*, PlayerObject*, float, SubtickPose& out) { out = {}; return false; }
void TrajectoryPredictionService::traceSubtickBranch(PlayLayer*, PlayerObject*, float, bool, cocos2d::CCDrawNode*, cocos2d::ccColor4F, float) {}
void TrajectoryPredictionService::setOverlaySuppressed(bool v) { m_overlaySuppressed = v; }
int TrajectoryPredictionService::survivesFor(PlayLayer*, PlayerObject*, int, int) { return -1; }
int TrajectoryPredictionService::survivesScript(PlayLayer*, PlayerObject*, int, ::std::vector<::std::pair<int, bool>> const&, ::std::vector<cocos2d::CCPoint>*) { return -1; }
void TrajectoryPredictionService::captureFrameDelta(float dt) { if (dt > 0.f) m_context.stepDelta = dt; }
void TrajectoryPredictionService::noteSimulatedDeath(PlayerObject*, GameObject*) {}
bool TrajectoryPredictionService::ownsPreviewPlayer(PlayerObject*) const { return false; }
int TrajectoryPredictionService::getSurvivedFrames(bool, bool) const { return 0; }
void TrajectoryPredictionService::onRealClick(bool, bool) {}
void TrajectoryPredictionService::simulateCollisionBatch(GJBaseGameLayer*, PlayerObject*, gd::vector<GameObject*>*, int, float) {}
bool TrajectoryPredictionService::handleActivationCheck(PlayerObject*, EffectGameObject*) { return false; }
void TrajectoryPredictionService::handleTouchedTrigger(PlayerObject*, EffectGameObject*) {}
} // namespace gucci
'@
[System.IO.File]::WriteAllText($traj, $trajStub, $utf8NoBom)
