#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../src/CameraSmoothing.hpp"
#include "../src/TFREngine.hpp"

// Utility macro for float comparison
#define ASSERT_NEAR(a, b, eps) \
    assert(std::abs((a) - (b)) < (eps))

void test_angle_wrapping() {
    std::cout << "[TEST] Running Angle Wrapping tests..." << std::endl;

    // Test normalizeAngleDeg
    ASSERT_NEAR(CameraSystem::normalizeAngleDeg(190.0f), -170.0f, 0.001f);
    ASSERT_NEAR(CameraSystem::normalizeAngleDeg(-200.0f), 160.0f, 0.001f);

    // Test shortest path lerpAngleDeg across -180 / 180 boundary
    // From -170 deg to +170 deg: direct shortest delta is -20 deg, not +340 deg!
    float angleMid = CameraSystem::lerpAngleDeg(-170.0f, 170.0f, 0.5f);
    // Intermediate angle should be -180 / +180 boundary
    ASSERT_NEAR(std::abs(angleMid), 180.0f, 0.01f);

    std::cout << "  -> Angle Wrapping test PASSED!" << std::endl;
}

void test_camera_smoothing_fps_independence() {
    std::cout << "[TEST] Running Camera Smoothing FPS Independence tests..." << std::endl;

    CameraSystem::CameraSmoothingEngine engine;
    engine.setEnabled(true);
    engine.setSmoothness(12.0f);

    // Set initial camera to (0, 0)
    engine.setTargetOrientation(0.0f, 0.0f, true);

    // Simulate mouse turn delta +90 yaw
    engine.updateRawInputDelta(90.0f, 0.0f);

    // Target yaw should immediately update to 90.0 without latency
    ASSERT_NEAR(engine.getCurrentState().targetYaw, 90.0f, 0.001f);

    // Simulate rendering at 60 FPS (dt = 0.01666s)
    auto state60 = engine.updateVisualCamera(0.0166667f);
    // Visual yaw should smoothly move towards 90 without reaching it instantly
    assert(state60.renderedYaw > 0.0f && state60.renderedYaw < 90.0f);

    // Simulate rendering at 120 FPS (dt = 0.00833s)
    engine.setTargetOrientation(0.0f, 0.0f, true);
    engine.updateRawInputDelta(90.0f, 0.0f);
    auto state120_1 = engine.updateVisualCamera(0.0083333f);
    auto state120_2 = engine.updateVisualCamera(0.0083333f);

    // Total distance traveled in two 120 FPS steps should match one 60 FPS step closely
    ASSERT_NEAR(state120_2.renderedYaw, state60.renderedYaw, 2.0f);

    std::cout << "  -> Camera Smoothing test PASSED!" << std::endl;
}

void test_tfr_motion_vectors_and_warp() {
    std::cout << "[TEST] Running TFR Motion Vectors and Spatial Warp tests..." << std::endl;

    TFR::TFREngine engine;
    engine.setEnabled(true);
    engine.setMode(TFR::TFRMode::MODE_1X);

    TFR::FrameMetadata frame0{ 1, 0.0, 10.0f, 0.0f, {0.0f, 0.0f, 0.0f}, false, false };
    TFR::FrameMetadata frame1{ 2, 0.0166, 20.0f, 0.0f, {0.0f, 0.0f, 0.0f}, false, false };

    engine.recordRealFrame(frame0, 0.0166);
    engine.recordRealFrame(frame1, 0.0166);

    // Motion should be +10 degrees yaw delta
    TFR::CameraMotionVector motion = engine.calculateCameraMotion(frame0, frame1);
    ASSERT_NEAR(motion.yawDelta, 10.0f, 0.001f);

    // Intermediate frame at progress 0.5 (G0.5)
    TFR::SpatialWarpParams warp = engine.computeGeneratedFrameWarp(0.5f);
    assert(!warp.fallbackToRealFrame);
    ASSERT_NEAR(warp.blendFactor, 0.5f, 0.001f);
    assert(warp.uOffset > 0.0f); // Screen UV shift calculated from motion vector

    std::cout << "  -> TFR Motion Vectors and Spatial Warp test PASSED!" << std::endl;
}

void test_tfr_fallbacks_and_disjunctions() {
    std::cout << "[TEST] Running TFR Fallbacks and Disjunction tests..." << std::endl;

    TFR::TFREngine engine;
    engine.setEnabled(true);
    engine.setMode(TFR::TFRMode::MODE_1X);

    // 1. Test Teleport Fallback
    TFR::FrameMetadata frame0{ 1, 0.0, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, false, false };
    TFR::FrameMetadata frameTeleport{ 2, 0.0166, 0.0f, 0.0f, {100.0f, 0.0f, 0.0f}, false, false };

    engine.recordRealFrame(frame0, 0.0166);
    engine.recordRealFrame(frameTeleport, 0.0166);

    TFR::SpatialWarpParams warpTeleport = engine.computeGeneratedFrameWarp(0.5f);
    assert(warpTeleport.fallbackToRealFrame == true);

    // 2. Test Camera Cut / Rapid Disjunction Fallback
    TFR::FrameMetadata frameCut{ 3, 0.0333, 120.0f, 0.0f, {100.0f, 0.0f, 0.0f}, true, false };
    engine.recordRealFrame(frameCut, 0.0166);

    TFR::SpatialWarpParams warpCut = engine.computeGeneratedFrameWarp(0.5f);
    assert(warpCut.fallbackToRealFrame == true);

    std::cout << "  -> TFR Fallbacks and Disjunction test PASSED!" << std::endl;
}

void test_tfr_adaptive_throttling() {
    std::cout << "[TEST] Running TFR Adaptive Throttling tests..." << std::endl;

    TFR::TFREngine engine;
    engine.setEnabled(true);
    engine.setMode(TFR::TFRMode::AUTO);

    TFR::FrameMetadata frame0{ 1, 0.0, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, false, false };
    // Simulate high frame time (dt = 0.05s -> 20 FPS, below adaptive 30 FPS threshold)
    engine.recordRealFrame(frame0, 0.050);

    assert(engine.isThrottled() == true);
    assert(engine.shouldGenerateFrame() == false);

    // Recover frame rate (dt = 0.0166s -> 60 FPS)
    TFR::FrameMetadata frame1{ 2, 0.050, 1.0f, 0.0f, {0.0f, 0.0f, 0.0f}, false, false };
    engine.recordRealFrame(frame1, 0.0166);

    assert(engine.isThrottled() == false);
    assert(engine.shouldGenerateFrame() == true);

    std::cout << "  -> TFR Adaptive Throttling test PASSED!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running Camera & TFR Suite Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    test_angle_wrapping();
    test_camera_smoothing_fps_independence();
    test_tfr_motion_vectors_and_warp();
    test_tfr_fallbacks_and_disjunctions();
    test_tfr_adaptive_throttling();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL UNIT TESTS PASSED SUCCESSFULLY!    " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
