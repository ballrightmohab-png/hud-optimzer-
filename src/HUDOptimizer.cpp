// HUDOptimizer.cpp - High-Performance HUD Rendering Engine for Minecraft Bedrock on LeviLaunchroid
// Architecture: Native C++20 module targeting Android ARM64

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace HUDOptimizer {

// ============================================================================
// Performance Presets
// ============================================================================

enum class PerformancePreset : int {
    Default = 0,
    Performance = 1,
    UltraPerformance = 2,
    Custom = 3
};

// ============================================================================
// Configuration & Settings State
// ============================================================================

struct Config {
    std::atomic<bool> enabled{true};
    std::atomic<int> preset{static_cast<int>(PerformancePreset::Default)};
    std::atomic<bool> adaptiveMode{true};
    std::atomic<bool> cleanHudMode{false};
    std::atomic<bool> cacheHotbar{true};
    std::atomic<bool> cacheVitals{true};
    std::atomic<bool> cacheText{true};
    std::atomic<bool> disableAnimations{true};
    std::atomic<bool> simplifyCrosshair{false};
    std::atomic<bool> reduceTransparency{false};
    std::atomic<bool> cameraSmoothing{true};
    std::atomic<bool> disableVsync{true};
    std::atomic<bool> entityCulling{true};
    std::atomic<bool> showProfiler{false};
};

inline Config& getConfig() noexcept {
    static Config instance;
    return instance;
}

void loadConfigFile(const std::filesystem::path& configDir) {
    try {
        std::filesystem::create_directories(configDir);
        std::filesystem::path filePath = configDir / "config.json";

        if (std::filesystem::exists(filePath)) {
            std::ifstream file(filePath);
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                auto& cfg = getConfig();
                if (j.contains("enabled")) cfg.enabled.store(j["enabled"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("preset")) cfg.preset.store(j["preset"].get<int>(), std::memory_order_relaxed);
                if (j.contains("adaptive_mode")) cfg.adaptiveMode.store(j["adaptive_mode"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("clean_hud")) cfg.cleanHudMode.store(j["clean_hud"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("cache_hotbar")) cfg.cacheHotbar.store(j["cache_hotbar"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("cache_vitals")) cfg.cacheVitals.store(j["cache_vitals"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("cache_text")) cfg.cacheText.store(j["cache_text"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("camera_smoothing")) cfg.cameraSmoothing.store(j["camera_smoothing"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("disable_vsync")) cfg.disableVsync.store(j["disable_vsync"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("disable_animations")) cfg.disableAnimations.store(j["disable_animations"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("simplify_crosshair")) cfg.simplifyCrosshair.store(j["simplify_crosshair"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("reduce_transparency")) cfg.reduceTransparency.store(j["reduce_transparency"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("entity_culling")) cfg.entityCulling.store(j["entity_culling"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("show_profiler")) cfg.showProfiler.store(j["show_profiler"].get<bool>(), std::memory_order_relaxed);
            }
        }
    } catch (...) {
        // Safe fallback to defaults if load fails
    }
}

void saveConfigFile(const std::filesystem::path& configDir) {
    try {
        std::filesystem::create_directories(configDir);
        std::filesystem::path filePath = configDir / "config.json";

        auto& cfg = getConfig();
        nlohmann::json j = {
            {"enabled", cfg.enabled.load(std::memory_order_relaxed)},
            {"preset", cfg.preset.load(std::memory_order_relaxed)},
            {"adaptive_mode", cfg.adaptiveMode.load(std::memory_order_relaxed)},
            {"clean_hud", cfg.cleanHudMode.load(std::memory_order_relaxed)},
            {"cache_hotbar", cfg.cacheHotbar.load(std::memory_order_relaxed)},
            {"cache_vitals", cfg.cacheVitals.load(std::memory_order_relaxed)},
            {"cache_text", cfg.cacheText.load(std::memory_order_relaxed)},
            {"camera_smoothing", cfg.cameraSmoothing.load(std::memory_order_relaxed)},
            {"disable_vsync", cfg.disableVsync.load(std::memory_order_relaxed)},
            {"disable_animations", cfg.disableAnimations.load(std::memory_order_relaxed)},
            {"simplify_crosshair", cfg.simplifyCrosshair.load(std::memory_order_relaxed)},
            {"reduce_transparency", cfg.reduceTransparency.load(std::memory_order_relaxed)},
            {"entity_culling", cfg.entityCulling.load(std::memory_order_relaxed)},
            {"show_profiler", cfg.showProfiler.load(std::memory_order_relaxed)}
        };

        std::ofstream file(filePath);
        if (file.is_open()) {
            file << j.dump(4);
        }
    } catch (...) {
        // Ignore save errors gracefully
    }
}

void applyPreset(PerformancePreset preset) {
    auto& cfg = getConfig();
    cfg.preset.store(static_cast<int>(preset), std::memory_order_relaxed);

    switch (preset) {
        case PerformancePreset::Default:
            cfg.cacheHotbar.store(true, std::memory_order_relaxed);
            cfg.cacheVitals.store(true, std::memory_order_relaxed);
            cfg.cacheText.store(true, std::memory_order_relaxed);
            cfg.disableAnimations.store(false, std::memory_order_relaxed);
            cfg.cleanHudMode.store(false, std::memory_order_relaxed);
            cfg.simplifyCrosshair.store(false, std::memory_order_relaxed);
            cfg.reduceTransparency.store(false, std::memory_order_relaxed);
            cfg.entityCulling.store(true, std::memory_order_relaxed);
            break;

        case PerformancePreset::Performance:
            cfg.cacheHotbar.store(true, std::memory_order_relaxed);
            cfg.cacheVitals.store(true, std::memory_order_relaxed);
            cfg.cacheText.store(true, std::memory_order_relaxed);
            cfg.disableAnimations.store(true, std::memory_order_relaxed);
            cfg.cleanHudMode.store(false, std::memory_order_relaxed);
            cfg.simplifyCrosshair.store(true, std::memory_order_relaxed);
            cfg.reduceTransparency.store(true, std::memory_order_relaxed);
            cfg.entityCulling.store(true, std::memory_order_relaxed);
            break;

        case PerformancePreset::UltraPerformance:
            cfg.cacheHotbar.store(true, std::memory_order_relaxed);
            cfg.cacheVitals.store(true, std::memory_order_relaxed);
            cfg.cacheText.store(true, std::memory_order_relaxed);
            cfg.disableAnimations.store(true, std::memory_order_relaxed);
            cfg.cleanHudMode.store(true, std::memory_order_relaxed);
            cfg.simplifyCrosshair.store(true, std::memory_order_relaxed);
            cfg.reduceTransparency.store(true, std::memory_order_relaxed);
            cfg.entityCulling.store(true, std::memory_order_relaxed);
            break;

        case PerformancePreset::Custom:
            break;
    }
}

// ============================================================================
// HudProfiler - Metrics & Performance Monitoring
// ============================================================================

class HudProfiler {
private:
    std::atomic<uint64_t> mTotalFrames{0};
    std::atomic<uint64_t> mCachedHits{0};
    std::atomic<uint64_t> mElementsRendered{0};
    std::atomic<uint64_t> mElementsSkipped{0};
    std::atomic<double> mLastFrameTimeMs{0.0};
    std::atomic<double> mAvgFrameTimeMs{0.0};
    std::chrono::high_resolution_clock::time_point mFrameStart;

public:
    void beginFrame() noexcept {
        mFrameStart = std::chrono::high_resolution_clock::now();
        mTotalFrames.fetch_add(1, std::memory_order_relaxed);
    }

    void endFrame() noexcept {
        auto end = std::chrono::high_resolution_clock::now();
        double durationMs = std::chrono::duration<double, std::milli>(end - mFrameStart).count();
        mLastFrameTimeMs.store(durationMs, std::memory_order_relaxed);

        double currentAvg = mAvgFrameTimeMs.load(std::memory_order_relaxed);
        mAvgFrameTimeMs.store(currentAvg * 0.95 + durationMs * 0.05, std::memory_order_relaxed);
    }

    void recordCacheHit() noexcept {
        mCachedHits.fetch_add(1, std::memory_order_relaxed);
    }

    void recordRendered(uint64_t count = 1) noexcept {
        mElementsRendered.fetch_add(count, std::memory_order_relaxed);
    }

    void recordSkipped(uint64_t count = 1) noexcept {
        mElementsSkipped.fetch_add(count, std::memory_order_relaxed);
    }

    [[nodiscard]] double getAvgFrameTimeMs() const noexcept {
        return mAvgFrameTimeMs.load(std::memory_order_relaxed);
    }

    [[nodiscard]] float getHitRate() const noexcept {
        uint64_t total = mTotalFrames.load(std::memory_order_relaxed);
        if (total == 0) return 0.0f;
        return (100.0f * mCachedHits.load(std::memory_order_relaxed)) / total;
    }

    std::string formatDebugInfo() const {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "HUD Opt | Frame: %.2f ms | Cache Hit: %.1f%% | Drawn: %llu | Skipped: %llu",
                      mAvgFrameTimeMs.load(std::memory_order_relaxed),
                      getHitRate(),
                      static_cast<unsigned long long>(mElementsRendered.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(mElementsSkipped.load(std::memory_order_relaxed)));
        return std::string(buf);
    }
};

inline HudProfiler& getProfiler() noexcept {
    static HudProfiler instance;
    return instance;
}

// ============================================================================
// Camera Smoothing Engine
// ============================================================================

class CameraSmoother {
private:
    float mSmoothYaw{0.0f};
    float mSmoothPitch{0.0f};
    bool mInitialized{false};
    mutable std::mutex mMutex;

public:
    void updateAndFilter(float& rawYaw, float& rawPitch, float deltaTime = 0.016f) noexcept {
        if (!getConfig().cameraSmoothing.load(std::memory_order_relaxed)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (!mInitialized) {
            mSmoothYaw = rawYaw;
            mSmoothPitch = rawPitch;
            mInitialized = true;
            return;
        }

        float deltaYaw = rawYaw - mSmoothYaw;
        float deltaPitch = rawPitch - mSmoothPitch;

        float alpha = 1.0f - std::exp(-18.0f * std::clamp(deltaTime, 0.001f, 0.1f));

        mSmoothYaw += deltaYaw * alpha;
        mSmoothPitch += deltaPitch * alpha;

        rawYaw = mSmoothYaw;
        rawPitch = mSmoothPitch;
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mInitialized = false;
    }
};

inline CameraSmoother& getCameraSmoother() noexcept {
    static CameraSmoother instance;
    return instance;
}

// ============================================================================
// Adaptive Performance Controller
// ============================================================================

class AdaptivePerformanceController {
private:
    std::atomic<bool> mDegradedMode{false};

public:
    void evaluate() noexcept {
        if (!getConfig().adaptiveMode.load(std::memory_order_relaxed)) {
            mDegradedMode.store(false, std::memory_order_relaxed);
            return;
        }

        double frameTimeMs = getProfiler().getAvgFrameTimeMs();
        if (frameTimeMs > 16.6) {
            mDegradedMode.store(true, std::memory_order_relaxed);
        } else if (frameTimeMs < 13.0) {
            mDegradedMode.store(false, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool shouldSuppressOptionalAnimations() const noexcept {
        return getConfig().disableAnimations.load(std::memory_order_relaxed) ||
               mDegradedMode.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool isDegraded() const noexcept {
        return mDegradedMode.load(std::memory_order_relaxed);
    }
};

inline AdaptivePerformanceController& getAdaptiveController() noexcept {
    static AdaptivePerformanceController instance;
    return instance;
}

// ============================================================================
// Zero-Allocation TextCache Engine
// ============================================================================

class TextCache {
private:
    struct TextEntry {
        std::string value;
        double lastUpdateTime{0.0};
    };

    std::unordered_map<std::string, TextEntry> mCache;
    mutable std::mutex mMutex;

public:
    bool shouldUpdateText(const std::string& key, const std::string& newValue) noexcept {
        if (!getConfig().cacheText.load(std::memory_order_relaxed)) {
            return true;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mCache.find(key);
        if (it == mCache.end()) {
            mCache[key] = {newValue, 0.0};
            return true;
        }

        if (it->second.value == newValue) {
            getProfiler().recordCacheHit();
            return false;
        }

        it->second.value = newValue;
        return true;
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mCache.clear();
    }
};

inline TextCache& getTextCache() noexcept {
    static TextCache instance;
    return instance;
}

// ============================================================================
// Hotbar State & Vitals Cache Engine (Dirty-Checking State Tracker)
// ============================================================================

struct SlotState {
    int itemId{0};
    int count{0};

    bool operator==(const SlotState& other) const noexcept {
        return itemId == other.itemId && count == other.count;
    }

    bool operator!=(const SlotState& other) const noexcept {
        return !(*this == other);
    }
};

struct VitalsState {
    int health{20};
    int hunger{20};
    int armor{0};
    int selectedSlot{0};

    bool operator==(const VitalsState& other) const noexcept {
        return health == other.health && hunger == other.hunger &&
               armor == other.armor && selectedSlot == other.selectedSlot;
    }

    bool operator!=(const VitalsState& other) const noexcept {
        return !(*this == other);
    }
};

class HotbarCache {
private:
    static constexpr size_t HOTBAR_SLOT_COUNT = 9;
    std::array<SlotState, HOTBAR_SLOT_COUNT> mSlots{};
    VitalsState mVitals{};
    bool mDirty{true};
    mutable std::mutex mMutex;

public:
    void invalidate() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mDirty = true;
    }

    bool updateSlot(size_t index, const SlotState& newState) noexcept {
        if (!getConfig().cacheHotbar.load(std::memory_order_relaxed) || index >= HOTBAR_SLOT_COUNT) {
            return true;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (mDirty || mSlots[index] != newState) {
            mSlots[index] = newState;
            return true;
        }

        getProfiler().recordCacheHit();
        return false;
    }

    bool updateVitals(const VitalsState& newVitals) noexcept {
        if (!getConfig().cacheVitals.load(std::memory_order_relaxed)) {
            return true;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (mDirty || mVitals != newVitals) {
            mVitals = newVitals;
            return true;
        }

        getProfiler().recordCacheHit();
        return false;
    }

    void finishFrame() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mDirty = false;
    }
};

inline HotbarCache& getHotbarCache() noexcept {
    static HotbarCache instance;
    return instance;
}

// ============================================================================
// Entity Culling State Manager
// ============================================================================

class EntityCuller {
private:
    std::atomic<uint64_t> mEntitiesCulled{0};

public:
    bool shouldRenderEntity(float distanceSquared, float maxDistanceSquared = 4096.0f) noexcept {
        if (!getConfig().entityCulling.load(std::memory_order_relaxed)) {
            return true;
        }

        if (distanceSquared > maxDistanceSquared) {
            mEntitiesCulled.fetch_add(1, std::memory_order_relaxed);
            getProfiler().recordSkipped(1);
            return false;
        }
        getProfiler().recordRendered(1);
        return true;
    }

    [[nodiscard]] uint64_t getCulledCount() const noexcept {
        return mEntitiesCulled.load(std::memory_order_relaxed);
    }
};

inline EntityCuller& getEntityCuller() noexcept {
    static EntityCuller instance;
    return instance;
}

// ============================================================================
// Active Clean HUD & Minimal Crosshair Overlay Engine
// ============================================================================

void renderCleanHudOverlay() noexcept {
    if (!getConfig().enabled.load(std::memory_order_relaxed)) {
        pl::modmenu::submitDrawCommands("hud_optimizer_module", {});
        return;
    }

    getProfiler().beginFrame();
    getAdaptiveController().evaluate();

    std::vector<pl::modmenu::DrawCommand> cmds;

    // Simplified Crosshair Overlay
    if (getConfig().simplifyCrosshair.load(std::memory_order_relaxed)) {
        pl::modmenu::DrawCommand crosshairHorizontal{};
        crosshairHorizontal.type = pl::modmenu::DrawCommandType::Line;
        crosshairHorizontal.x = -6.0f;
        crosshairHorizontal.y = 0.0f;
        crosshairHorizontal.w = 12.0f;
        crosshairHorizontal.h = 0.0f;
        crosshairHorizontal.color = 0xFFFFFFFF;
        crosshairHorizontal.size = 2.0f;
        cmds.push_back(crosshairHorizontal);

        pl::modmenu::DrawCommand crosshairVertical{};
        crosshairVertical.type = pl::modmenu::DrawCommandType::Line;
        crosshairVertical.x = 0.0f;
        crosshairVertical.y = -6.0f;
        crosshairVertical.w = 0.0f;
        crosshairVertical.h = 12.0f;
        crosshairVertical.color = 0xFFFFFFFF;
        crosshairVertical.size = 2.0f;
        cmds.push_back(crosshairVertical);

        getProfiler().recordRendered(2);
    } else {
        getProfiler().recordSkipped(2);
    }

    // Clean HUD Mode Status Indicator
    if (getConfig().cleanHudMode.load(std::memory_order_relaxed)) {
        pl::modmenu::DrawCommand textCmd{};
        textCmd.type = pl::modmenu::DrawCommandType::Text;
        textCmd.x = 10.0f;
        textCmd.y = 10.0f;
        textCmd.color = 0xFF00FF00;
        textCmd.size = 14.0f;
        textCmd.text = "HUD Opt [Clean Mode]";
        cmds.push_back(textCmd);
        getProfiler().recordRendered(1);
    }

    // Performance Debug Profiler Info
    if (getConfig().showProfiler.load(std::memory_order_relaxed)) {
        pl::modmenu::DrawCommand profilerCmd{};
        profilerCmd.type = pl::modmenu::DrawCommandType::Text;
        profilerCmd.x = 10.0f;
        profilerCmd.y = 30.0f;
        profilerCmd.color = 0xFFFFFF00;
        profilerCmd.size = 12.0f;
        profilerCmd.text = getProfiler().formatDebugInfo();
        cmds.push_back(profilerCmd);
    }

    pl::modmenu::submitDrawCommands("hud_optimizer_module", cmds);
    getProfiler().endFrame();
}

// ============================================================================
// Bedrock UI Detours & Interception Hooks
// ============================================================================

namespace Hooks {

// Stored target addresses
void* targetEglSwapBuffers = nullptr;

// Original function pointers
int (*orig_EglSwapBuffers)(void*, void*) = nullptr;

// Per-Frame Render Interception Detour
int hook_EglSwapBuffers(void* dpy, void* surface) {
    // Process per-frame active overlays during EGL buffer swap
    renderCleanHudOverlay();

    if (orig_EglSwapBuffers) {
        return orig_EglSwapBuffers(dpy, surface);
    }
    return 0;
}

bool installHooks() noexcept {
    void* eglHandle = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (eglHandle) {
        targetEglSwapBuffers = dlsym(eglHandle, "eglSwapBuffers");
    }

    if (targetEglSwapBuffers) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(targetEglSwapBuffers),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_EglSwapBuffers),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_EglSwapBuffers));
    }

    return (targetEglSwapBuffers != nullptr);
}

void uninstallHooks() noexcept {
    if (targetEglSwapBuffers) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(targetEglSwapBuffers), reinterpret_cast<pl::memory::FuncPtr>(hook_EglSwapBuffers));
        targetEglSwapBuffers = nullptr;
        orig_EglSwapBuffers = nullptr;
    }
}

} // namespace Hooks

// ============================================================================
// Global State Management
// ============================================================================

void invalidateAllCaches() noexcept {
    getHotbarCache().invalidate();
    getTextCache().clear();
    getCameraSmoother().reset();
}

} // namespace HUDOptimizer

// ============================================================================
// Native Mod Lifecycle Handler
// ============================================================================

class HUDOptimizerMod {
private:
    ll::mod::NativeMod& mSelf;

    void registerModMenuControls() {
        using namespace pl::modmenu;

        auto& cfg = HUDOptimizer::getConfig();

        // Register ModMenu Builder
        ModuleBuilder builder("hud_optimizer_module", "HUD Optimizer");
        builder.description("High-performance HUD optimization engine for Bedrock with adaptive performance mode, entity culling, presets, and VSync control.")
               .modId("hud_optimizer")
               .defaultEnabled(true)
               .hideInHudEditor(false)
               .config("enabled", "Enable Optimizer Engine", ConfigType::Toggle, cfg.enabled.load() ? "true" : "false")
               .config("preset", "Performance Preset", ConfigType::SliderInt, std::to_string(cfg.preset.load()), "0", "3")
               .config("adaptive_mode", "Adaptive Performance Mode", ConfigType::Toggle, cfg.adaptiveMode.load() ? "true" : "false")
               .config("clean_hud", "Clean HUD Mode", ConfigType::Toggle, cfg.cleanHudMode.load() ? "true" : "false")
               .config("cache_hotbar", "Cache Hotbar Slots", ConfigType::Toggle, cfg.cacheHotbar.load() ? "true" : "false")
               .config("cache_vitals", "Cache Health/Hunger/Armor/XP", ConfigType::Toggle, cfg.cacheVitals.load() ? "true" : "false")
               .config("cache_text", "Cache UI Text Layouts", ConfigType::Toggle, cfg.cacheText.load() ? "true" : "false")
               .config("disable_vsync", "Disable VSync (Max FPS)", ConfigType::Toggle, cfg.disableVsync.load() ? "true" : "false")
               .config("entity_culling", "Entity Distance Culling", ConfigType::Toggle, cfg.entityCulling.load() ? "true" : "false")
               .config("camera_smoothing", "Smooth Camera Turning", ConfigType::Toggle, cfg.cameraSmoothing.load() ? "true" : "false")
               .config("disable_animations", "Disable Cosmetic Animations", ConfigType::Toggle, cfg.disableAnimations.load() ? "true" : "false")
               .config("simplify_crosshair", "Simplify Crosshair", ConfigType::Toggle, cfg.simplifyCrosshair.load() ? "true" : "false")
               .config("reduce_transparency", "Reduce Layers & Transparency", ConfigType::Toggle, cfg.reduceTransparency.load() ? "true" : "false")
               .config("show_profiler", "Show Profiler Info", ConfigType::Toggle, cfg.showProfiler.load() ? "true" : "false")
               .onToggle([this](std::string_view moduleId, bool enabled) {
                   HUDOptimizer::getConfig().enabled.store(enabled, std::memory_order_relaxed);
                   HUDOptimizer::invalidateAllCaches();
                   HUDOptimizer::saveConfigFile(mSelf.getConfigDir());
               })
               .onConfigChanged([this](std::string_view moduleId, std::string_view key, std::string_view value) {
                   auto& cfgState = HUDOptimizer::getConfig();
                   bool boolVal = (value == "true" || value == "1");

                   if (key == "enabled") {
                       cfgState.enabled.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "preset") {
                       try {
                           int presetVal = std::stoi(std::string(value));
                           HUDOptimizer::applyPreset(static_cast<HUDOptimizer::PerformancePreset>(presetVal));
                       } catch (...) {}
                   } else if (key == "adaptive_mode") {
                       cfgState.adaptiveMode.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "clean_hud") {
                       cfgState.cleanHudMode.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_hotbar") {
                       cfgState.cacheHotbar.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_vitals") {
                       cfgState.cacheVitals.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_text") {
                       cfgState.cacheText.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "disable_vsync") {
                       cfgState.disableVsync.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "entity_culling") {
                       cfgState.entityCulling.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "camera_smoothing") {
                       cfgState.cameraSmoothing.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "disable_animations") {
                       cfgState.disableAnimations.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "simplify_crosshair") {
                       cfgState.simplifyCrosshair.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "reduce_transparency") {
                       cfgState.reduceTransparency.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "show_profiler") {
                       cfgState.showProfiler.store(boolVal, std::memory_order_relaxed);
                   }

                   HUDOptimizer::invalidateAllCaches();
                   HUDOptimizer::saveConfigFile(mSelf.getConfigDir());
               });

        if (builder.registerModule()) {
            mSelf.getLogger().info("HUD Optimizer ModMenu module registered successfully.");
        } else {
            mSelf.getLogger().warn("Failed to register HUD Optimizer ModMenu module.");
        }

        // Register Quick Toggle On-Screen Button
        ButtonBuilder toggleButton("hud_opt_toggle_btn", "HUD Optimizer Toggle");
        toggleButton.moduleId("hud_optimizer_module")
                    .modId("hud_optimizer")
                    .label("HUD Opt: ON")
                    .behavior(ButtonBehavior::Toggle)
                    .defaultVisible(true)
                    .stylePreset(ButtonStylePreset::Accent)
                    .onEvent([this](std::string_view buttonId, ButtonEvent event, float value) {
                        if (event == ButtonEvent::Click || event == ButtonEvent::StateChanged) {
                            bool newVisible = value > 0.5f;
                            HUDOptimizer::getConfig().enabled.store(newVisible, std::memory_order_relaxed);
                            HUDOptimizer::invalidateAllCaches();
                            HUDOptimizer::saveConfigFile(mSelf.getConfigDir());
                        }
                    });

        if (toggleButton.registerButton()) {
            mSelf.getLogger().info("HUD Optimizer ModMenu quick toggle button registered.");
        }
    }

public:
    static HUDOptimizerMod& instance() noexcept {
        static HUDOptimizerMod mod;
        return mod;
    }

    HUDOptimizerMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() noexcept {
        mSelf.getLogger().info("Loading HUD Optimizer Native Engine...");
        HUDOptimizer::loadConfigFile(mSelf.getConfigDir());
        return true;
    }

    bool enable() noexcept {
        mSelf.getLogger().info("Enabling HUD Optimizer...");
        HUDOptimizer::loadConfigFile(mSelf.getConfigDir());
        HUDOptimizer::invalidateAllCaches();

        bool hooksInstalled = HUDOptimizer::Hooks::installHooks();
        if (hooksInstalled) {
            mSelf.getLogger().info("Native HUD interception hooks installed successfully.");
        } else {
            mSelf.getLogger().warn("Native HUD hooks partially or not installed; operating in safe mode.");
        }

        registerModMenuControls();
        mSelf.getLogger().info("HUD Optimizer enabled successfully!");
        return true;
    }

    bool disable() noexcept {
        mSelf.getLogger().info("Disabling HUD Optimizer...");
        HUDOptimizer::getConfig().enabled.store(false, std::memory_order_relaxed);
        HUDOptimizer::invalidateAllCaches();
        HUDOptimizer::Hooks::uninstallHooks();
        pl::modmenu::unregisterButton("hud_opt_toggle_btn");
        pl::modmenu::unregisterModule("hud_optimizer_module");
        return true;
    }

    bool unload() noexcept {
        mSelf.getLogger().info("Unloading HUD Optimizer...");
        return true;
    }
};

PL_REGISTER_MOD(
    HUDOptimizerMod,
    HUDOptimizerMod::instance()
);
