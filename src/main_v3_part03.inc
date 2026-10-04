        }

        g_trialEndFrame = trialEndFrameFor(g_analysisIndex, g_probeShift);
        return true;
    }
}

void queueTrialReset() {
    if (g_trialQueued || g_mode != Mode::Analyzing) return;
    g_trialQueued = true;
    g_trialActive = false;
    Loader::get()->queueInMainThread([] {
        if (g_mode != Mode::Analyzing) {
            g_trialQueued = false;
            return;
        }
        if (auto* pl = PlayLayer::get()) pl->resetLevel();
    });
}

void onTrialResult(bool success) {
    if (g_mode != Mode::Analyzing || g_analysisIndex >= g_macro.size()) return;
    g_trialActive = false;

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
    if (prepareProbeOrAdvance()) queueTrialReset();
}

void startAnalysis() {
    if (!g_macroCompleted || g_macro.empty()) {
        FLAlertLayer::create(
            "Frame Windows",
            "Record or replay one complete run first. xdBot replay capture is supported at 240 TPS.",
            "OK"
        )->show();
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
    g_trialQueued = false;
    g_trialActive = false;
    g_trialEndFrame = -1;
    clearMarkers();
    refreshHud();

    if (prepareProbeOrAdvance()) queueTrialReset();
}

void startPlayback() {
    if (!g_macroCompleted || g_macro.empty()) {
        FLAlertLayer::create("Frame Windows", "No complete macro captured yet.", "OK")->show();
        return;
    }

    g_liveBins.fill(0);
    g_mode = Mode::Playback;
    g_lastProcessedFrame = -1;
    refreshHud();
    Loader::get()->queueInMainThread([] {
        if (auto* pl = PlayLayer::get()) pl->resetLevel();
    });
}

} // namespace fw

class $modify(FWBaseGameLayerV3, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player2) {
        if (fw::g_mode == fw::Mode::Recording && !fw::g_injecting)
            fw::recordTransition(down, button, player2);

        GJBaseGameLayer::handleButton(down, button, player2);
    }

    void processCommands(float dt) {
        // Match xdBot's timing order on GD 2.2074: let the game advance first,
        // then use levelTime*240 to decide which macro frame fires.
        GJBaseGameLayer::processCommands(dt);

        int64_t frame = fw::current240Frame();
        fw::g_frame = frame;

        // processCommands may be invoked more than once while still on the same
        // 240-TPS tick. Never replay / count the same click twice.
        if (frame != fw::g_lastProcessedFrame) {
            fw::g_lastProcessedFrame = frame;
            fw::injectForFrame(this, frame);
        }

        if (fw::g_mode == fw::Mode::Analyzing && fw::g_trialActive &&
            fw::g_trialEndFrame >= 0 && frame >= fw::g_trialEndFrame) {
            fw::onTrialResult(true);
        }
    }
};

class $modify(FWPlayLayerV3, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        fw::g_frame = fw::current240Frame();
        fw::g_lastProcessedFrame = -1;
        fw::g_injecting = false;
        fw::g_macroCompleted = false;
        fw::g_currentRun.clear();
        fw::g_bestRun.clear();
        fw::g_macro.clear();
        fw::g_mode = fw::Mode::Recording;
        fw::g_liveBins.fill(0);
        fw::g_trialQueued = false;
        fw::g_trialActive = false;
        fw::g_trialEndFrame = -1;
        fw::resetRecordedHeld();
        fw::clearMarkers();

        auto size = CCDirector::sharedDirector()->getWinSize();
        fw::g_statusHud = CCLabelBMFont::create("CAPTURE 240  0 inputs", "bigFont.fnt");
        fw::g_statusHud->setAnchorPoint({0.f, 1.f});
        fw::g_statusHud->setScale(0.26f);
        fw::g_statusHud->setPosition({7.f, size.height - 7.f});
        this->addChild(fw::g_statusHud, 9999);

        for (int i = 0; i < 8; ++i) {
            fw::g_binHud[i] = CCLabelBMFont::create("", "bigFont.fnt");
            if (!fw::g_binHud[i]) continue;
            fw::g_binHud[i]->setAnchorPoint({0.f, 1.f});
            fw::g_binHud[i]->setScale(0.26f);
            fw::g_binHud[i]->setPosition({7.f, size.height - 7.f - i * 11.f});
            fw::g_binHud[i]->setColor(fw::colorForBin(i));
            fw::g_binHud[i]->setVisible(false);
            this->addChild(fw::g_binHud[i], 9999);
        }
        return true;
    }
