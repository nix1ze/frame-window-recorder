#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
#include <array>
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

enum class Mode {
    Recording,
    Idle,
    Playback,
    Analyzing,
};

enum class ProbeDirection {
    Early,
    Late,
};

static Mode g_mode = Mode::Recording;
static int64_t g_frame = 0;
static bool g_injecting = false;
static bool g_macroCompleted = false;
static bool g_trialQueued = false;

static std::vector<InputEvent> g_currentRun;
static std::vector<InputEvent> g_macro;

static size_t g_analysisIndex = 0;
static ProbeDirection g_probeDir = ProbeDirection::Early;
static int g_probeShift = -1;
static int g_early = 0;
static int g_late = 0;
static int g_completedMeasurements = 0;

static std::array<int, 8> g_liveBins{};
static CCLabelBMFont* g_hud = nullptr;

int maxShift() {
    return Mod::get()->getSettingValue<int>("max-shift");
}

bool analyzeReleases() {
    return Mod::get()->getSettingValue<bool>("analyze-releases");
}

int injectOffset() {
    return Mod::get()->getSettingValue<int>("inject-offset");
}

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

std::string binsText(std::array<int, 8> const& bins) {
    std::ostringstream ss;
    ss << "0-1: " << bins[0] << "\n";
    ss << "2:   " << bins[1] << "\n";
    ss << "3:   " << bins[2] << "\n";
    ss << "4:   " << bins[3] << "\n";
    ss << "5-6: " << bins[4] << "\n";
    ss << "7-8: " << bins[5] << "\n";
    ss << "9-10:" << bins[6] << "\n";
    ss << "11+: " << bins[7];
    return ss.str();
}

std::array<int, 8> allBins() {
    std::array<int, 8> bins{};
    for (auto const& e : g_macro) {
        if (e.measured && e.window > 0) {
            bins[binForWindow(e.window)]++;
        }
    }
    return bins;
}

void refreshHud() {
    if (!g_hud) return;

    std::string text;
    switch (g_mode) {
        case Mode::Recording:
            text = "REC  " + std::to_string(g_currentRun.size()) + " inputs";
            break;
        case Mode::Idle: {
            if (!g_macroCompleted) {
                text = "NO COMPLETE MACRO";
            } else {
                text = binsText(allBins());
            }
            break;
        }
        case Mode::Playback:
            text = binsText(g_liveBins);
            break;
        case Mode::Analyzing: {
            std::ostringstream ss;
            ss << "CALC " << g_completedMeasurements << "/" << g_macro.size() << "\n";
            ss << "input " << (g_analysisIndex + 1) << "  shift " << g_probeShift;
            text = ss.str();
            break;
        }
    }

    g_hud->setString(text.c_str());
}

void saveMacroCsv() {
    auto dir = Mod::get()->getConfigDir();
    std::filesystem::create_directories(dir);

    std::ofstream out(dir / "last_macro.csv", std::ios::trunc);
    out << "index,frame,button,player1,down,x,y\n";
    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        out << i << ',' << e.frame << ',' << e.button << ','
            << (e.player1 ? 1 : 0) << ',' << (e.down ? 1 : 0) << ','
            << e.x << ',' << e.y << '\n';
    }
}

void saveWindowsCsv() {
    auto dir = Mod::get()->getConfigDir();
    std::filesystem::create_directories(dir);

    std::ofstream out(dir / "frame_windows.csv", std::ios::trunc);
    out << "index,frame,button,player1,down,early,late,window,x,y\n";
    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        out << i << ',' << e.frame << ',' << e.button << ','
            << (e.player1 ? 1 : 0) << ',' << (e.down ? 1 : 0) << ','
            << e.early << ',' << e.late << ',' << e.window << ','
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
    const auto newFrame = cur.frame + shift;
    if (newFrame < 0) return false;

    if (auto prev = previousSameControl(index)) {
        if (newFrame <= g_macro[*prev].frame) return false;
    }

    if (cur.down) {
        if (auto rel = pairedRelease(index)) {
            auto relFrame = g_macro[*rel].frame + shift;
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

int64_t effectiveFrame(size_t eventIndex) {
    int64_t frame = g_macro[eventIndex].frame;
    if (g_mode != Mode::Analyzing) return frame;

    if (eventIndex == g_analysisIndex) return frame + g_probeShift;

    if (g_analysisIndex < g_macro.size() && g_macro[g_analysisIndex].down) {
        if (auto rel = pairedRelease(g_analysisIndex); rel && *rel == eventIndex) {
            return frame + g_probeShift;
        }
    }
    return frame;
}

void injectForCurrentFrame(GJBaseGameLayer* layer) {
    if (g_mode != Mode::Playback && g_mode != Mode::Analyzing) return;

    const int64_t targetFrame = g_frame + injectOffset();
    for (size_t i = 0; i < g_macro.size(); ++i) {
        if (effectiveFrame(i) != targetFrame) continue;

        auto const& e = g_macro[i];
        g_injecting = true;
        layer->handleButton(e.down, e.button, e.player1);
        g_injecting = false;

        if (g_mode == Mode::Playback && e.measured && e.window > 0) {
            g_liveBins[binForWindow(e.window)]++;
            refreshHud();
        }
    }
}

bool moveToNextAnalyzable() {
    while (g_analysisIndex < g_macro.size() && !isAnalyzable(g_analysisIndex)) {
        g_analysisIndex++;
    }
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

bool prepareProbeOrAdvance();

bool prepareProbeOrAdvance() {
    while (true) {
        if (!moveToNextAnalyzable()) {
            g_mode = Mode::Idle;
            saveWindowsCsv();
            refreshHud();
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
        return true;
    }
}

void queueNextTrial();

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
    else {
        Loader::get()->queueInMainThread([] {
            if (auto pl = PlayLayer::get()) pl->resetLevel();
        });
    }
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
    g_early = 0;
    g_late = 0;
    g_completedMeasurements = 0;

    refreshHud();
    if (!prepareProbeOrAdvance()) return;
    queueNextTrial();
}

void startPlayback() {
    if (!g_macroCompleted || g_macro.empty()) {
        FLAlertLayer::create("Frame Windows", "No complete macro recorded yet.", "OK")->show();
        return;
    }

    g_liveBins.fill(0);
    g_mode = Mode::Playback;
    refreshHud();
    Loader::get()->queueInMainThread([] {
        if (auto pl = PlayLayer::get()) pl->resetLevel();
    });
}

} // namespace fw

class $modify(FWBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        if (fw::g_mode == fw::Mode::Recording && !fw::g_injecting) {
            fw::InputEvent e;
            e.frame = fw::g_frame;
            e.down = down;
            e.button = button;
            e.player1 = player1;

            auto pl = PlayLayer::get();
            if (pl) {
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

    void processCommands(float dt, bool p1, bool p2) {
        fw::injectForCurrentFrame(this);
        GJBaseGameLayer::processCommands(dt, p1, p2);
        fw::g_frame++;
    }
};

class $modify(FWPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        fw::g_frame = 0;
        fw::g_injecting = false;
        fw::g_macroCompleted = false;
        fw::g_currentRun.clear();
        fw::g_macro.clear();
        fw::g_mode = fw::Mode::Recording;
        fw::g_liveBins.fill(0);

        auto size = CCDirector::sharedDirector()->getWinSize();
        fw::g_hud = CCLabelBMFont::create("REC  0 inputs", "bigFont.fnt");
        fw::g_hud->setAnchorPoint({0.f, 1.f});
        fw::g_hud->setScale(0.28f);
        fw::g_hud->setPosition({7.f, size.height - 7.f});
        fw::g_hud->setZOrder(9999);
        this->addChild(fw::g_hud);

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        fw::g_frame = 0;
        fw::g_injecting = false;

        if (fw::g_mode == fw::Mode::Recording) {
            fw::g_currentRun.clear();
        }
        fw::refreshHud();
    }

    void levelComplete() {
        if (fw::g_mode == fw::Mode::Analyzing) {
            fw::onTrialResult(true);
            return;
        }

        if (fw::g_mode == fw::Mode::Playback) {
            fw::g_mode = fw::Mode::Idle;
            fw::refreshHud();
            Notification::create("Macro replay complete", NotificationIcon::Success)->show();
            Loader::get()->queueInMainThread([] {
                if (auto pl = PlayLayer::get()) pl->resetLevel();
            });
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
            fw::onTrialResult(false);
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void onQuit() {
        fw::g_hud = nullptr;
        fw::g_injecting = false;
        fw::g_trialQueued = false;
        PlayLayer::onQuit();
    }
};

class $modify(FWPauseLayer, PauseLayer) {
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
        auto analyzeBtn = CCMenuItemSpriteExtra::create(
            analyzeSpr,
            this,
            menu_selector(FWPauseLayer::onFWAnalyze)
        );
        analyzeBtn->setPosition({-55.f, 0.f});
        menu->addChild(analyzeBtn);

        auto replaySpr = ButtonSprite::create("Replay");
        replaySpr->setScale(0.65f);
        auto replayBtn = CCMenuItemSpriteExtra::create(
            replaySpr,
            this,
            menu_selector(FWPauseLayer::onFWReplay)
        );
        replayBtn->setPosition({55.f, 0.f});
        menu->addChild(replayBtn);
    }
};
