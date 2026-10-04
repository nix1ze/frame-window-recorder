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
