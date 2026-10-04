}

void refreshHud() {
    if (!g_statusHud) return;

    if (g_mode == Mode::Recording) {
        setBinsVisible(false);
        g_statusHud->setVisible(true);
        std::ostringstream ss;
        ss << "CAPTURE 240  " << g_currentRun.size() << " inputs";
        if (!g_bestRun.empty()) ss << "\nbest attempt: " << g_bestRun.size();
        g_statusHud->setString(ss.str().c_str());
        return;
    }

    if (g_mode == Mode::Analyzing) {
        setBinsVisible(false);
        g_statusHud->setVisible(true);
        std::ostringstream ss;
        ss << "240 TPS  CALC " << g_completedMeasurements << "/" << g_macro.size() << "\n";
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
    auto* root = CCNode::create();
    auto* draw = CCDrawNode::create();
    if (!root || !draw) return root;

    int bin = binForWindow(window);
    auto color = color4ForBin(bin);
    float radius = 9.5f + std::min(window, 12) * 0.30f;
    constexpr int segments = 42;

    // Thin outlined circle like the reference rather than a filled marker.
    for (int i = 0; i < segments; ++i) {
        float a0 = static_cast<float>(i) / segments * 6.28318530718f;
        float a1 = static_cast<float>(i + 1) / segments * 6.28318530718f;
        CCPoint p0{std::cos(a0) * radius, std::sin(a0) * radius};
        CCPoint p1{std::cos(a1) * radius, std::sin(a1) * radius};
        draw->drawSegment(p0, p1, 0.80f, color);
    }
    root->addChild(draw);

    auto* label = CCLabelBMFont::create(std::to_string(window).c_str(), "bigFont.fnt");
    if (label) {
        label->setScale(0.20f);
        label->setColor(colorForBin(bin));
        label->setPosition({-radius - 5.5f, 0.f});
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
        CCScaleTo::create(0.055f, 1.50f),
        CCScaleTo::create(0.16f, 1.f),
        nullptr
    ));

    // Short bright chime. We keep it to a built-in GD resource so the .geode
    // stays self-contained on 2.2074.
    FMODAudioEngine::sharedEngine()->playEffect("achievement_01.ogg");
}

void saveMacroCsv() {
    auto dir = Mod::get()->getConfigDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / "last_macro.csv", std::ios::trunc);
    out << "index,frame240,button,player2,down,x,y\n";
    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        out << i << ',' << e.frame << ',' << e.button << ','
            << (e.player2 ? 1 : 0) << ',' << (e.down ? 1 : 0) << ','
            << e.x << ',' << e.y << '\n';
    }
}

void saveWindowsCsv() {
    auto dir = Mod::get()->getConfigDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / "frame_windows.csv", std::ios::trunc);
    out << "index,frame240,button,player2,down,early,late,window,x,y\n";
    for (size_t i = 0; i < g_macro.size(); ++i) {
        auto const& e = g_macro[i];
        out << i << ',' << e.frame << ',' << e.button << ','
            << (e.player2 ? 1 : 0) << ',' << (e.down ? 1 : 0) << ','
            << e.early << ',' << e.late << ',' << e.window << ','
            << e.x << ',' << e.y << '\n';
    }
}

void rememberBestAttempt() {
    if (g_currentRun.size() > g_bestRun.size())
        g_bestRun = g_currentRun;
}

// Capture exactly one state transition. This is the important xdBot fix: its
// replay can cause the hook chain to see the same logical state more than once;
// repeated DOWN while already down (or repeated UP while already up) is not a
// new click and must not become another marker / analysis target.
void recordTransition(bool down, int button, bool player2) {
    if (button < 0 || button >= 4) return;
    int p = player2 ? 1 : 0;
    if (g_recordHeld[p][button] == down) return;
    g_recordHeld[p][button] = down;

    InputEvent e;
    e.frame = current240Frame();
    e.down = down;
    e.button = button;
    e.player2 = player2;

    if (auto* pl = PlayLayer::get()) {
        auto* player = player2 ? pl->m_player2 : pl->m_player1;
