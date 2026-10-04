#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace fw {

struct InputEvent {
    int64_t frame = 0;
    bool down = false;
    int button = 1;
    bool player1 = true;
    float x = 0.f;
    float y = 0.f;
    bool measured = false;
    int early = 0;
    int late = 0;
    int window = 0;
};

enum class Mode { Recording, Idle, Playback, Analyzing };
enum class ProbeDirection { Early, Late };

static Mode g_mode = Mode::Recording;
static int64_t g_frame = 0;
static bool g_injecting = false;
static bool g_macroCompleted = false;
static bool g_trialQueued = false;
static bool g_trialJudged = false;
static int64_t g_trialEndFrame = -1;

static std::vector<InputEvent> g_currentRun;
static std::vector<InputEvent> g_macro;
static size_t g_analysisIndex = 0;
static ProbeDirection g_probeDir = ProbeDirection::Early;
static int g_probeShift = -1;
static int g_early = 0;
static int g_late = 0;
static int g_completedMeasurements = 0;

static std::array<int, 8> g_liveBins{};
static CCLabelBMFont* g_statusHud = nullptr;
static std::array<CCLabelBMFont*, 8> g_binHud{};
static CCNode* g_markerRoot = nullptr;
static std::vector<CCNode*> g_markers;

int settingInt(char const* key) {
    return static_cast<int>(Mod::get()->getSettingValue<int64_t>(key));
}

int maxShift() { return settingInt("max-shift"); }
int injectOffset() { return settingInt("inject-offset"); }
int validationFrames() { return settingInt("validation-frames"); }
int nextInputSlack() { return settingInt("next-input-slack"); }
bool analyzeReleases() { return Mod::get()->getSettingValue<bool>("analyze-releases"); }

int binForWindow(int w) {
    if (w <= 1) return 0;
    if (w == 2) return 1;
    if (w == 3) return 2;
    if (w == 4) return 3;
    if (w <= 6) return 4;
    if (w <= 8) return 5;
    if (w <= 10) return 6;
    return 7;
}

ccColor3B colorForBin(int bin) {
    static std::array<ccColor3B, 8> const colors = {
        ccColor3B{255, 68, 68},
        ccColor3B{255, 145, 45},
        ccColor3B{255, 210, 55},
        ccColor3B{200, 235, 75},
        ccColor3B{95, 225, 115},
        ccColor3B{70, 220, 205},
        ccColor3B{105, 165, 255},
        ccColor3B{205, 120, 255}
    };
    return colors[std::clamp(bin, 0, 7)];
}

ccColor4F color4ForBin(int bin) {
    auto c = colorForBin(bin);
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f};
}

std::array<int, 8> allBins() {
    std::array<int, 8> bins{};
    for (auto const& e : g_macro) {
        if (e.down && e.measured && e.window > 0) bins[binForWindow(e.window)]++;
    }
    return bins;
}

void setBinsVisible(bool visible) {
    for (auto* label : g_binHud) if (label) label->setVisible(visible);
}

void refreshBinHud(std::array<int, 8> const& bins) {
    static std::array<char const*, 8> const names = {"0-1", "2", "3", "4", "5-6", "7-8", "9-10", "11+"};
    for (int i = 0; i < 8; ++i) {
        if (!g_binHud[i]) continue;
        auto text = std::string(names[i]) + ": " + std::to_string(bins[i]);
        g_binHud[i]->setString(text.c_str());
        g_binHud[i]->setColor(colorForBin(i));
    }
}

void refreshHud() {
    if (!g_statusHud) return;

    if (g_mode == Mode::Recording) {
        setBinsVisible(false);
        g_statusHud->setVisible(true);
        auto text = "REC  " + std::to_string(g_currentRun.size()) + " inputs";
        g_statusHud->setString(text.c_str());
        return;
    }

    if (g_mode == Mode::Analyzing) {
        setBinsVisible(false);
        g_statusHud->setVisible(true);
        std::ostringstream ss;
        ss << "CALC " << g_completedMeasurements << "/" << g_macro.size() << "\n";
        ss << "input " << (g_analysisIndex + 1) << "  shift " << g_probeShift;
        g_statusHud->setString(ss.str().c_str());
        return;
    }

    if (g_mode == Mode::Idle && !g_macroCompleted) {
        setBinsVisible(false);
        g_statusHud->setVisible(true);
        g_statusHud->setString("NO COMPLETE MACRO");
        return;
    }

    g_statusHud->setVisible(false);
    setBinsVisible(true);
    refreshBinHud(g_mode == Mode::Playback ? g_liveBins : allBins());
}

void clearMarkers() {
    if (g_markerRoot) {
        g_markerRoot->removeFromParentAndCleanup(true);
        g_markerRoot = nullptr;
    }
    g_markers.clear();
}

CCNode* makeMarker(int window) {
    auto root = CCNode::create();
    auto draw = CCDrawNode::create();
    if (!root || !draw) return root;

    int bin = binForWindow(window);
    auto color = color4ForBin(bin);
    float radius = 11.f + std::min(window, 12) * 0.35f;
    constexpr int segments = 40;
    for (int i = 0; i < segments; ++i) {
        float a0 = static_cast<float>(i) / segments * 6.28318530718f;
        float a1 = static_cast<float>(i + 1) / segments * 6.28318530718f;
        CCPoint p0{std::cos(a0) * radius, std::sin(a0) * radius};
        CCPoint p1{std::cos(a1) * radius, std::sin(a1) * radius};
        draw->drawSegment(p0, p1, 0.95f, color);
    }
    root->addChild(draw);

    auto label = CCLabelBMFont::create(std::to_string(window).c_str(), "bigFont.fnt");
    if (label) {
        label->setScale(0.24f);
        label->setColor(colorForBin(bin));
        label->setPosition({-radius - 7.f, 0.f});
        root->addChild(label);
    }
    return root;
}

void rebuildMarkers(PlayLayer* pl) {
    clearMarkers();
    if (!pl || !pl->m_objectLayer) return;

    g_markerRoot = CCNode::create();
    pl->m_objectLayer->addChild(g_markerRoot, 9999);
    g_markers.assign(g_macro.size(), nullptr);

    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        if (!e.down || !e.measured || e.window <= 0) continue;
        auto* marker = makeMarker(e.window);
        if (!marker) continue;
        marker->setPosition({e.x, e.y});
        g_markerRoot->addChild(marker);
        g_markers[i] = marker;
    }
}

void pulseMarker(size_t index) {
    if (index >= g_markers.size() || !g_markers[index]) return;
    auto* marker = g_markers[index];
    marker->stopAllActions();
    marker->setScale(1.f);
    marker->runAction(CCSequence::create(
        CCScaleTo::create(0.07f, 1.45f),
        CCScaleTo::create(0.16f, 1.f),
        nullptr
    ));
    FMODAudioEngine::sharedEngine()->playEffect("achievement_01.ogg");
}

void saveMacroCsv() {
    auto dir = Mod::get()->getConfigDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / "last_macro.csv", std::ios::trunc);
    out << "index,frame,button,player1,down,x,y\n";
    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        out << i << ',' << e.frame << ',' << e.button << ',' << (e.player1 ? 1 : 0) << ','
            << (e.down ? 1 : 0) << ',' << e.x << ',' << e.y << '\n';
    }
}

void saveWindowsCsv() {
    auto dir = Mod::get()->getConfigDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / "frame_windows.csv", std::ios::trunc);
    out << "index,frame,button,player1,down,early,late,window,x,y\n";
    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        out << i << ',' << e.frame << ',' << e.button << ',' << (e.player1 ? 1 : 0) << ','
            << (e.down ? 1 : 0) << ',' << e.early << ',' << e.late << ',' << e.window << ','
            << e.x << ',' << e.y << '\n';
    }
}

bool isAnalyzable(size_t i) {
    if (i >= g_macro.size()) return false;
    auto const& e = g_macro[i];
    if (e.button != 1) return false;
    if (!analyzeReleases() && !e.down) return false;
    return true;
}

std::optional<size_t> pairedRelease(size_t pressIndex) {
    if (pressIndex >= g_macro.size() || !g_macro[pressIndex].down) return std::nullopt;
    auto const& p = g_macro[pressIndex];
    for (size_t i = pressIndex + 1; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        if (e.button == p.button && e.player1 == p.player1) {
            if (!e.down) return i;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<size_t> previousSameControl(size_t index) {
    if (index == 0 || index >= g_macro.size()) return std::nullopt;
    auto const& cur = g_macro[index];
    for (size_t i = index; i-- > 0;) {
        auto const& e = g_macro[i];
        if (e.button == cur.button && e.player1 == cur.player1) return i;
    }
    return std::nullopt;
}

std::optional<size_t> nextSameControl(size_t index) {
    if (index >= g_macro.size()) return std::nullopt;
    auto const& cur = g_macro[index];
    for (size_t i = index + 1; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        if (e.button == cur.button && e.player1 == cur.player1) return i;
    }
    return std::nullopt;
}

bool shiftIsValid(size_t index, int shift) {
    if (index >= g_macro.size()) return false;
    auto const& cur = g_macro[index];
    int64_t newFrame = cur.frame + shift;
    if (newFrame < 0) return false;

    if (auto prev = previousSameControl(index)) {
        if (newFrame <= g_macro[*prev].frame) return false;
    }

    if (cur.down) {
        if (auto rel = pairedRelease(index)) {
            int64_t relFrame = g_macro[*rel].frame + shift;
            if (relFrame <= newFrame) return false;
            if (auto next = nextSameControl(*rel)) {
                if (relFrame >= g_macro[*next].frame) return false;
            }
        } else if (auto next = nextSameControl(index)) {
            if (newFrame >= g_macro[*next].frame) return false;
        }
    } else if (auto next = nextSameControl(index)) {
        if (newFrame >= g_macro[*next].frame) return false;
    }
    return true;
}

std::optional<int64_t> nextIndependentInputFrame(size_t index) {
    if (index >= g_macro.size()) return std::nullopt;
    size_t begin = index + 1;
    if (g_macro[index].down) {
        if (auto rel = pairedRelease(index)) begin = *rel + 1;
    }
    for (size_t i = begin; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        if (e.button == 1 && e.down) return e.frame;
    }
    return std::nullopt;
}

int64_t trialEndFrameFor(size_t index, int shift) {
    int64_t shifted = g_macro[index].frame + shift - injectOffset();
    int64_t end = shifted + std::max(1, validationFrames());
    if (auto next = nextIndependentInputFrame(index)) {
        end = std::min(end, *next - injectOffset() - std::max(0, nextInputSlack()));
    }
    if (end <= shifted) end = shifted + 1;
    return end;
}

int64_t effectiveFrame(size_t eventIndex) {
    int64_t frame = g_macro[eventIndex].frame;
    if (g_mode != Mode::Analyzing) return frame;
    if (eventIndex == g_analysisIndex) return frame + g_probeShift;
    if (g_analysisIndex < g_macro.size() && g_macro[g_analysisIndex].down) {
        if (auto rel = pairedRelease(g_analysisIndex); rel && *rel == eventIndex) return frame + g_probeShift;
    }
    return frame;
}

void injectForCurrentFrame(GJBaseGameLayer* layer) {
    if (g_mode != Mode::Playback && g_mode != Mode::Analyzing) return;
    int64_t targetFrame = g_frame + injectOffset();
    for (size_t i = 0; i < g_macro.size(); ++i) {
        if (effectiveFrame(i) != targetFrame) continue;
        auto const& e = g_macro[i];
        g_injecting = true;
        layer->handleButton(e.down, e.button, e.player1);
        g_injecting = false;
        if (g_mode == Mode::Playback && e.down && e.measured && e.window > 0) {
            g_liveBins[binForWindow(e.window)]++;
            pulseMarker(i);
            refreshHud();
        }
    }
}

bool moveToNextAnalyzable() {
    while (g_analysisIndex < g_macro.size() && !isAnalyzable(g_analysisIndex)) g_analysisIndex++;
    return g_analysisIndex < g_macro.size();
}

void finishCurrentInput() {
    auto& e = g_macro[g_analysisIndex];
    e.measured = true;
    e.early = g_early;
    e.late = g_late;
    e.window = g_early + g_late + 1;
    g_completedMeasurements++;
    g_analysisIndex++;
    g_early = 0;
    g_late = 0;
    g_probeDir = ProbeDirection::Early;
    g_probeShift = -1;
}

void queueNextTrial();

bool prepareProbeOrAdvance() {
    while (true) {
        if (!moveToNextAnalyzable()) {
            g_mode = Mode::Idle;
            saveWindowsCsv();
            refreshHud();
            rebuildMarkers(PlayLayer::get());
            Notification::create("Frame-window analysis complete", NotificationIcon::Success)->show();
            return false;
        }

        if (std::abs(g_probeShift) > maxShift() || !shiftIsValid(g_analysisIndex, g_probeShift)) {
            if (g_probeDir == ProbeDirection::Early) {
                g_probeDir = ProbeDirection::Late;
                g_probeShift = 1;
                continue;
            }
            finishCurrentInput();
            continue;
        }

        g_trialEndFrame = trialEndFrameFor(g_analysisIndex, g_probeShift);
        g_trialJudged = false;
        return true;
    }
}

void onTrialResult(bool success) {
    if (g_mode != Mode::Analyzing || g_analysisIndex >= g_macro.size()) return;

    if (success) {
        if (g_probeDir == ProbeDirection::Early) {
            g_early = std::max(g_early, -g_probeShift);
            g_probeShift--;
        } else {
            g_late = std::max(g_late, g_probeShift);
            g_probeShift++;
        }
    } else {
        if (g_probeDir == ProbeDirection::Early) {
            g_probeDir = ProbeDirection::Late;
            g_probeShift = 1;
        } else {
            finishCurrentInput();
        }
    }

    refreshHud();
    if (prepareProbeOrAdvance()) queueNextTrial();
    else Loader::get()->queueInMainThread([] { if (auto pl = PlayLayer::get()) pl->resetLevel(); });
}

void queueNextTrial() {
    if (g_trialQueued) return;
    g_trialQueued = true;
    Loader::get()->queueInMainThread([] {
        g_trialQueued = false;
        if (g_mode != Mode::Analyzing) return;
        if (auto pl = PlayLayer::get()) pl->resetLevel();
    });
}

void startAnalysis() {
    if (!g_macroCompleted || g_macro.empty()) {
        FLAlertLayer::create("Frame Windows", "Finish the level once first so a complete macro can be recorded.", "OK")->show();
        return;
    }

    for (auto& e : g_macro) {
        e.measured = false;
        e.early = e.late = e.window = 0;
    }

    g_mode = Mode::Analyzing;
    g_analysisIndex = 0;
    g_probeDir = ProbeDirection::Early;
    g_probeShift = -1;
    g_early = g_late = 0;
    g_completedMeasurements = 0;
    g_trialEndFrame = -1;
    g_trialJudged = false;
    clearMarkers();
    refreshHud();
    if (prepareProbeOrAdvance()) queueNextTrial();
}

void startPlayback() {
    if (!g_macroCompleted || g_macro.empty()) {
        FLAlertLayer::create("Frame Windows", "No complete macro recorded yet.", "OK")->show();
        return;
    }
    g_liveBins.fill(0);
    g_mode = Mode::Playback;
    rebuildMarkers(PlayLayer::get());
    refreshHud();
    Loader::get()->queueInMainThread([] { if (auto pl = PlayLayer::get()) pl->resetLevel(); });
}

} // namespace fw

class $modify(FWBaseGameLayerV2, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        if (fw::g_mode == fw::Mode::Recording && !fw::g_injecting) {
            fw::InputEvent e;
            e.frame = fw::g_frame;
            e.down = down;
            e.button = button;
            e.player1 = player1;
            if (auto pl = PlayLayer::get()) {
                auto player = player1 ? pl->m_player1 : pl->m_player2;
                if (player) {
                    e.x = player->getPositionX();
                    e.y = player->getPositionY();
                }
            }
            fw::g_currentRun.push_back(e);
            fw::refreshHud();
        }
        GJBaseGameLayer::handleButton(down, button, player1);
    }

    void processCommands(float dt) {
        fw::injectForCurrentFrame(this);
        GJBaseGameLayer::processCommands(dt);
        fw::g_frame++;
        if (fw::g_mode == fw::Mode::Analyzing && !fw::g_trialJudged &&
            fw::g_trialEndFrame >= 0 && fw::g_frame >= fw::g_trialEndFrame) {
            fw::g_trialJudged = true;
            fw::onTrialResult(true);
        }
    }
};

class $modify(FWPlayLayerV2, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        fw::g_frame = 0;
        fw::g_injecting = false;
        fw::g_macroCompleted = false;
        fw::g_currentRun.clear();
        fw::g_macro.clear();
        fw::g_mode = fw::Mode::Recording;
        fw::g_liveBins.fill(0);
        fw::g_trialEndFrame = -1;
        fw::g_trialJudged = false;
        fw::clearMarkers();

        auto size = CCDirector::sharedDirector()->getWinSize();
        fw::g_statusHud = CCLabelBMFont::create("REC  0 inputs", "bigFont.fnt");
        fw::g_statusHud->setAnchorPoint({0.f, 1.f});
        fw::g_statusHud->setScale(0.28f);
        fw::g_statusHud->setPosition({7.f, size.height - 7.f});
        this->addChild(fw::g_statusHud, 9999);

        for (int i = 0; i < 8; ++i) {
            fw::g_binHud[i] = CCLabelBMFont::create("", "bigFont.fnt");
            if (!fw::g_binHud[i]) continue;
            fw::g_binHud[i]->setAnchorPoint({0.f, 1.f});
            fw::g_binHud[i]->setScale(0.28f);
            fw::g_binHud[i]->setPosition({7.f, size.height - 7.f - i * 12.f});
            fw::g_binHud[i]->setColor(fw::colorForBin(i));
            fw::g_binHud[i]->setVisible(false);
            this->addChild(fw::g_binHud[i], 9999);
        }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        fw::g_frame = 0;
        fw::g_injecting = false;
        if (fw::g_mode == fw::Mode::Analyzing) fw::g_trialJudged = false;
        if (fw::g_mode == fw::Mode::Recording) fw::g_currentRun.clear();
        fw::refreshHud();
    }

    void levelComplete() {
        if (fw::g_mode == fw::Mode::Analyzing) {
            if (!fw::g_trialJudged) {
                fw::g_trialJudged = true;
                fw::onTrialResult(true);
            }
            return;
        }

        if (fw::g_mode == fw::Mode::Playback) {
            fw::g_mode = fw::Mode::Idle;
            fw::refreshHud();
            Notification::create("Macro replay complete", NotificationIcon::Success)->show();
            Loader::get()->queueInMainThread([] { if (auto pl = PlayLayer::get()) pl->resetLevel(); });
            return;
        }

        if (fw::g_mode == fw::Mode::Recording) {
            fw::g_macro = fw::g_currentRun;
            fw::g_macroCompleted = !fw::g_macro.empty();
            if (fw::g_macroCompleted) {
                fw::saveMacroCsv();
                Notification::create(
                    "Macro recorded: " + std::to_string(fw::g_macro.size()) + " inputs",
                    NotificationIcon::Success
                )->show();
            }
        }
        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (fw::g_mode == fw::Mode::Analyzing) {
            if (!fw::g_trialJudged) {
                fw::g_trialJudged = true;
                fw::onTrialResult(false);
            }
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void onQuit() {
        fw::clearMarkers();
        fw::g_statusHud = nullptr;
        fw::g_binHud.fill(nullptr);
        fw::g_injecting = false;
        fw::g_trialQueued = false;
        fw::g_trialEndFrame = -1;
        fw::g_trialJudged = false;
        PlayLayer::onQuit();
    }
};

class $modify(FWPauseLayerV2, PauseLayer) {
    void onFWAnalyze(CCObject*) {
        this->onResume(nullptr);
        fw::startAnalysis();
    }

    void onFWReplay(CCObject*) {
        this->onResume(nullptr);
        fw::startPlayback();
    }

    void customSetup() {
        PauseLayer::customSetup();
        auto size = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, 32.f});
        this->addChild(menu, 50);

        auto analyzeSpr = ButtonSprite::create("Analyze");
        analyzeSpr->setScale(0.65f);
        auto analyzeBtn = CCMenuItemSpriteExtra::create(analyzeSpr, this, menu_selector(FWPauseLayerV2::onFWAnalyze));
        analyzeBtn->setPosition({-55.f, 0.f});
        menu->addChild(analyzeBtn);

        auto replaySpr = ButtonSprite::create("Replay");
        replaySpr->setScale(0.65f);
        auto replayBtn = CCMenuItemSpriteExtra::create(replaySpr, this, menu_selector(FWPauseLayerV2::onFWReplay));
        replayBtn->setPosition({55.f, 0.f});
        menu->addChild(replayBtn);
    }
};
