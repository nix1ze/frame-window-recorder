        if (player) {
            e.x = player->getPositionX();
            e.y = player->getPositionY();
        }
    }

    // Extra exact-event guard for hook-chain duplicates in the same 240 tick.
    if (!g_currentRun.empty()) {
        auto const& prev = g_currentRun.back();
        if (prev.frame == e.frame && prev.button == e.button &&
            prev.player2 == e.player2 && prev.down == e.down)
            return;
    }

    g_currentRun.push_back(e);
    refreshHud();
}

bool isAnalyzable(size_t i) {
    if (i >= g_macro.size()) return false;
    auto const& e = g_macro[i];
    if (e.button != 1) return false;
    if (!analyzeReleases() && !e.down) return false;
    return true;
}

std::optional<size_t> previousSameControl(size_t index) {
    if (index == 0 || index >= g_macro.size()) return std::nullopt;
    auto const& cur = g_macro[index];
    for (size_t i = index; i-- > 0;) {
        auto const& e = g_macro[i];
        if (e.button == cur.button && e.player2 == cur.player2) return i;
    }
    return std::nullopt;
}

std::optional<size_t> nextSameControl(size_t index) {
    if (index >= g_macro.size()) return std::nullopt;
    auto const& cur = g_macro[index];
    for (size_t i = index + 1; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        if (e.button == cur.button && e.player2 == cur.player2) return i;
    }
    return std::nullopt;
}

// Measure ONE transition. Old builds moved a press and its release together;
// that was measuring a translated hold, not the actual frame window of the
// click. Presses now move alone; releases move alone only when enabled.
bool shiftIsValid(size_t index, int shift) {
    if (index >= g_macro.size()) return false;
    int64_t newFrame = g_macro[index].frame + shift;
    if (newFrame < 1) return false;

    if (auto prev = previousSameControl(index)) {
        if (newFrame <= g_macro[*prev].frame) return false;
    }
    if (auto next = nextSameControl(index)) {
        if (newFrame >= g_macro[*next].frame) return false;
    }
    return true;
}

std::optional<int64_t> nextPressFrame(size_t index) {
    if (index >= g_macro.size()) return std::nullopt;
    for (size_t i = index + 1; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        if (e.button == 1 && e.down) return e.frame;
    }
    return std::nullopt;
}

int64_t effectiveFrame(size_t eventIndex) {
    int64_t frame = g_macro[eventIndex].frame;
    if (g_mode == Mode::Analyzing && eventIndex == g_analysisIndex)
        frame += g_probeShift;
    return frame;
}

int64_t trialEndFrameFor(size_t index, int shift) {
    int64_t shifted = g_macro[index].frame + shift;
    int64_t end = shifted + std::max(1, validationFrames());

    // Do not blame the current click for what a later press does. This is
    // especially important on dense wave spam / Solar-Flare-like gameplay.
    if (auto next = nextPressFrame(index)) {
        end = std::min(end, *next - std::max(0, nextInputSlack()));
    }

    if (end <= shifted) end = shifted + 1;
    return end;
}

void injectForFrame(GJBaseGameLayer* layer, int64_t currentFrame) {
    if (g_mode != Mode::Playback && g_mode != Mode::Analyzing) return;
    if (g_mode == Mode::Analyzing && !g_trialActive) return;

    int64_t target = currentFrame + injectOffset();
    for (size_t i = 0; i < g_macro.size(); ++i) {
        if (effectiveFrame(i) != target) continue;

        auto const& e = g_macro[i];
        g_injecting = true;
        layer->handleButton(e.down, e.button, e.player2);
        g_injecting = false;

        if (g_mode == Mode::Playback && e.down && e.measured && e.window > 0) {
            g_liveBins[binForWindow(e.window)]++;
            pulseMarker(i);
            refreshHud();
        }
    }
}

bool moveToNextAnalyzable() {
    while (g_analysisIndex < g_macro.size() && !isAnalyzable(g_analysisIndex))
        g_analysisIndex++;
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

bool prepareProbeOrAdvance() {
    while (true) {
        if (!moveToNextAnalyzable()) {
            g_mode = Mode::Idle;
            g_trialActive = false;
            g_trialQueued = false;
            g_trialEndFrame = -1;
            saveWindowsCsv();
            refreshHud();
            Notification::create("Frame-window analysis complete", NotificationIcon::Success)->show();
            Loader::get()->queueInMainThread([] {
                if (auto* pl = PlayLayer::get()) pl->resetLevel();
            });
            return false;
        }

        if (std::abs(g_probeShift) > maxShift() ||
            !shiftIsValid(g_analysisIndex, g_probeShift)) {
            if (g_probeDir == ProbeDirection::Early) {
                g_probeDir = ProbeDirection::Late;
                g_probeShift = 1;
                continue;
            }
            finishCurrentInput();
            continue;
