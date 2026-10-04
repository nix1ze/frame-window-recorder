
    void resetLevel() {
        // Preserve the best failed attempt for the HUD instead of making the
        // capture look like all clicks simply vanished on every death.
        if (fw::g_mode == fw::Mode::Recording)
            fw::rememberBestAttempt();

        PlayLayer::resetLevel();

        fw::g_frame = fw::current240Frame();
        fw::g_lastProcessedFrame = -1;
        fw::g_injecting = false;

        if (fw::g_mode == fw::Mode::Recording) {
            fw::g_currentRun.clear();
            fw::resetRecordedHeld();
        } else if (fw::g_mode == fw::Mode::Analyzing) {
            fw::g_trialQueued = false;
            fw::g_trialActive = true;
        }

        if (fw::g_mode == fw::Mode::Idle || fw::g_mode == fw::Mode::Playback)
            fw::rebuildMarkers(this);

        fw::refreshHud();
    }

    void levelComplete() {
        if (fw::g_mode == fw::Mode::Analyzing) {
            if (fw::g_trialActive) fw::onTrialResult(true);
            return;
        }

        if (fw::g_mode == fw::Mode::Playback) {
            fw::g_mode = fw::Mode::Idle;
            fw::refreshHud();
            Notification::create("Frame-window replay complete", NotificationIcon::Success)->show();
            PlayLayer::levelComplete();
            return;
        }

        if (fw::g_mode == fw::Mode::Recording) {
            fw::g_macro = fw::g_currentRun;
            fw::g_macroCompleted = !fw::g_macro.empty();
            if (fw::g_macroCompleted) {
                fw::saveMacroCsv();
                Notification::create(
                    "Captured 240-TPS macro: " + std::to_string(fw::g_macro.size()) + " transitions",
                    NotificationIcon::Success
                )->show();
            }
        }

        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (fw::g_mode == fw::Mode::Analyzing) {
            if (fw::g_trialActive) fw::onTrialResult(false);
            // Do not let the failed probe run the normal death flow; the next
            // probe reset is already queued.
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
        fw::g_trialActive = false;
        fw::g_trialEndFrame = -1;
        fw::g_lastProcessedFrame = -1;
        PlayLayer::onQuit();
    }
};

class $modify(FWPauseLayerV3, PauseLayer) {
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
        auto* menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, 32.f});
        this->addChild(menu, 50);

        auto* analyzeSpr = ButtonSprite::create("Analyze 240");
        analyzeSpr->setScale(0.62f);
        auto* analyzeBtn = CCMenuItemSpriteExtra::create(
            analyzeSpr, this, menu_selector(FWPauseLayerV3::onFWAnalyze)
        );
        analyzeBtn->setPosition({-62.f, 0.f});
        menu->addChild(analyzeBtn);

        auto* replaySpr = ButtonSprite::create("Replay");
        replaySpr->setScale(0.62f);
        auto* replayBtn = CCMenuItemSpriteExtra::create(
            replaySpr, this, menu_selector(FWPauseLayerV3::onFWReplay)
        );
        replayBtn->setPosition({62.f, 0.f});
        menu->addChild(replayBtn);
    }
};
