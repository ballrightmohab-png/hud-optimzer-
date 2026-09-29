#pragma once

#include <cmath>
#include <atomic>
#include <algorithm>

namespace CameraSystem {

// Utility math helper functions for camera angle interpolation with NaN safety
inline float normalizeAngleDeg(float angle) noexcept {
    if (!std::isfinite(angle)) return 0.0f;
    float norm = std::fmod(angle, 360.0f);
    if (norm > 180.0f) norm -= 360.0f;
    if (norm < -180.0f) norm += 360.0f;
    return norm;
}

inline float lerpAngleDeg(float current, float target, float alpha) noexcept {
    float diff = normalizeAngleDeg(target - current);
    return normalizeAngleDeg(current + diff * alpha);
}

inline float lerpScalar(float start, float end, float alpha) noexcept {
    return start + (end - start) * alpha;
}

class CameraSmoothingEngine {
public:
    struct State {
        float targetYaw{0.0f};
        float targetPitch{0.0f};
        float renderedYaw{0.0f};
        float renderedPitch{0.0f};
    };

private:
    std::atomic<bool> mEnabled{true};
    std::atomic<float> mSmoothness{12.0f}; // Smoothness factor (higher = tighter follow)

    // Target camera state (immediately updated by raw input)
    std::atomic<float> mTargetYaw{0.0f};
    std::atomic<float> mTargetPitch{0.0f};

    // Interpolated visual camera state (used for rendering only)
    float mRenderedYaw{0.0f};
    float mRenderedPitch{0.0f};

    bool mInitialized{false};

public:
    CameraSmoothingEngine() = default;

    void setEnabled(bool enabled) noexcept {
        mEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool isEnabled() const noexcept {
        return mEnabled.load(std::memory_order_relaxed);
    }

    void setSmoothness(float smoothness) noexcept {
        mSmoothness.store(std::clamp(smoothness, 1.0f, 50.0f), std::memory_order_relaxed);
    }

    float getSmoothness() const noexcept {
        return mSmoothness.load(std::memory_order_relaxed);
    }

    // Called immediately on raw mouse/touch input. Updates target rotation with zero latency.
    void updateRawInputDelta(float yawDelta, float pitchDelta) noexcept {
        float curTargetYaw = mTargetYaw.load(std::memory_order_relaxed);
        float curTargetPitch = mTargetPitch.load(std::memory_order_relaxed);

        float newTargetYaw = normalizeAngleDeg(curTargetYaw + yawDelta);
        float newTargetPitch = std::clamp(curTargetPitch + pitchDelta, -89.9f, 89.9f);

        mTargetYaw.store(newTargetYaw, std::memory_order_relaxed);
        mTargetPitch.store(newTargetPitch, std::memory_order_relaxed);

        if (!mInitialized) {
            mRenderedYaw = newTargetYaw;
            mRenderedPitch = newTargetPitch;
            mInitialized = true;
        }
    }

    // Directly set absolute target orientation (e.g., initial spawn, teleport, or snap)
    void setTargetOrientation(float yaw, float pitch, bool snapRendered = false) noexcept {
        float normYaw = normalizeAngleDeg(yaw);
        float normPitch = std::clamp(pitch, -89.9f, 89.9f);

        mTargetYaw.store(normYaw, std::memory_order_relaxed);
        mTargetPitch.store(normPitch, std::memory_order_relaxed);

        if (snapRendered || !mInitialized) {
            mRenderedYaw = normYaw;
            mRenderedPitch = normPitch;
            mInitialized = true;
        }
    }

    // Called on render frame update with actual frame time delta (dt in seconds).
    // Computes visual camera interpolation frame-rate independently.
    State updateVisualCamera(float dt) noexcept {
        float targetYaw = mTargetYaw.load(std::memory_order_relaxed);
        float targetPitch = mTargetPitch.load(std::memory_order_relaxed);

        if (!mInitialized) {
            mRenderedYaw = targetYaw;
            mRenderedPitch = targetPitch;
            mInitialized = true;
            return { targetYaw, targetPitch, targetYaw, targetPitch };
        }

        if (!mEnabled.load(std::memory_order_relaxed) || dt <= 0.0f) {
            mRenderedYaw = targetYaw;
            mRenderedPitch = targetPitch;
            return { targetYaw, targetPitch, targetYaw, targetPitch };
        }

        // Frame-time based exponential smoothing factor: alpha = 1.0 - exp(-smoothness * dt)
        float smoothness = mSmoothness.load(std::memory_order_relaxed);
        float alpha = 1.0f - std::exp(-smoothness * dt);
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        // Angle-aware interpolation for yaw (handles 180 / -180 wrapping smoothly)
        mRenderedYaw = lerpAngleDeg(mRenderedYaw, targetYaw, alpha);

        // Linear interpolation for pitch (clamped -89.9 to 89.9 degrees)
        mRenderedPitch = lerpScalar(mRenderedPitch, targetPitch, alpha);
        mRenderedPitch = std::clamp(mRenderedPitch, -89.9f, 89.9f);

        return { targetYaw, targetPitch, mRenderedYaw, mRenderedPitch };
    }

    // Apply smoothed visual camera angles directly to game entity/camera matrix pointer
    void applyToCameraContext(void* cameraCtx, float renderedYaw, float renderedPitch) noexcept {
        if (!cameraCtx) return;
        // Apply visual yaw and pitch to camera orientation fields in Bedrock Render Context
        float* cameraRotPtr = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(cameraCtx) + 0x18);
        cameraRotPtr[0] = renderedYaw;
        cameraRotPtr[1] = renderedPitch;
    }

    State getCurrentState() const noexcept {
        return {
            mTargetYaw.load(std::memory_order_relaxed),
            mTargetPitch.load(std::memory_order_relaxed),
            mRenderedYaw,
            mRenderedPitch
        };
    }
};

inline CameraSmoothingEngine& getCameraEngine() noexcept {
    static CameraSmoothingEngine instance;
    return instance;
}

} // namespace CameraSystem
