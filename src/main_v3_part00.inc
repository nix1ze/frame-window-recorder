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
    int64_t frame = 0;          // always 240 TPS frame number
    bool down = false;
    int button = 1;
    bool player2 = false;       // matches GD / xdBot semantics
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

static constexpr double kTPS = 240.0;

static Mode g_mode = Mode::Recording;
static int64_t g_frame = 0;
static int64_t g_lastProcessedFrame = -1;
static bool g_injecting = false;
static bool g_macroCompleted = false;

static std::vector<InputEvent> g_currentRun;
static std::vector<InputEvent> g_bestRun;
static std::vector<InputEvent> g_macro;

// [player2?][button 0..3]. Used to suppress duplicate transitions coming from
// other bots / repeated hook calls. A click is one press transition, not every
// handleButton call while the state is already held.
static std::array<std::array<bool, 4>, 2> g_recordHeld{};

static size_t g_analysisIndex = 0;
static ProbeDirection g_probeDir = ProbeDirection::Early;
static int g_probeShift = -1;
static int g_early = 0;
static int g_late = 0;
static int g_completedMeasurements = 0;

static bool g_trialQueued = false;
static bool g_trialActive = false;
static int64_t g_trialEndFrame = -1;

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

// xdBot 2.4.x records at int(levelTime * TPS) + 1. Using the exact same clock
// is important: counting processCommands calls is NOT a 240 FPS clock and was
// the reason old builds could turn nearly every input into a fake 1-frame.
int64_t current240Frame() {
    auto* pl = PlayLayer::get();
    if (!pl) return 0;
    auto frame = static_cast<int64_t>(pl->m_gameState.m_levelTime * kTPS) + 1;
    return std::max<int64_t>(0, frame);
}

void resetRecordedHeld() {
    for (auto& p : g_recordHeld) p.fill(false);
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

ccColor3B colorForBin(int bin) {
    static std::array<ccColor3B, 8> const colors = {
        ccColor3B{255, 60, 80},    // 0-1 red/pink
        ccColor3B{255, 145, 45},   // 2 orange
        ccColor3B{255, 210, 55},   // 3 yellow
        ccColor3B{195, 235, 70},   // 4 lime
        ccColor3B{90, 225, 110},   // 5-6 green
        ccColor3B{65, 225, 205},   // 7-8 cyan
        ccColor3B{90, 165, 255},   // 9-10 blue
        ccColor3B{200, 115, 255}   // 11+ purple
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
        if (e.down && e.measured && e.window > 0)
            bins[binForWindow(e.window)]++;
    }
    return bins;
}

void setBinsVisible(bool visible) {
    for (auto* label : g_binHud)
        if (label) label->setVisible(visible);
}

void refreshBinHud(std::array<int, 8> const& bins) {
    static std::array<char const*, 8> const names = {
        "0-1", "2", "3", "4", "5-6", "7-8", "9-10", "11+"
    };
    for (int i = 0; i < 8; ++i) {
        if (!g_binHud[i]) continue;
        auto text = std::string(names[i]) + ": " + std::to_string(bins[i]);
        g_binHud[i]->setString(text.c_str());
        g_binHud[i]->setColor(colorForBin(i));
    }
