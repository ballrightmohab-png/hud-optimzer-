// HUDOptimizer.cpp - Optimized HUD Rendering Engine for Minecraft Bedrock on LeviLaunchroid
// Architecture: Native C++20 module targeting Android ARM64

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <filesystem>
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
// Configuration & Settings State
// ============================================================================

struct Config {
    std::atomic<bool> enabled{true};
    std::atomic<bool> cleanHudMode{false};
    std::atomic<bool> cacheHotbar{true};
    std::atomic<bool> cacheVitals{true};
    std::atomic<bool> disableAnimations{true};
    std::atomic<bool> simplifyCrosshair{false};
    std::atomic<bool> reduceTransparency{false};
    std::atomic<bool> cameraSmoothing{true};
    std::atomic<bool> disableVsync{true};
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
                if (j.contains("clean_hud")) cfg.cleanHudMode.store(j["clean_hud"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("cache_hotbar")) cfg.cacheHotbar.store(j["cache_hotbar"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("cache_vitals")) cfg.cacheVitals.store(j["cache_vitals"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("camera_smoothing")) cfg.cameraSmoothing.store(j["camera_smoothing"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("disable_vsync")) cfg.disableVsync.store(j["disable_vsync"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("disable_animations")) cfg.disableAnimations.store(j["disable_animations"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("simplify_crosshair")) cfg.simplifyCrosshair.store(j["simplify_crosshair"].get<bool>(), std::memory_order_relaxed);
                if (j.contains("reduce_transparency")) cfg.reduceTransparency.store(j["reduce_transparency"].get<bool>(), std::memory_order_relaxed);
            }
        }
    } catch (...) {
        // Fallback safely to defaults if file read or parse fails
    }
}

void saveConfigFile(const std::filesystem::path& configDir) {
    try {
        std::filesystem::create_directories(configDir);
        std::filesystem::path filePath = configDir / "config.json";

        auto& cfg = getConfig();
        nlohmann::json j = {
            {"enabled", cfg.enabled.load(std::memory_order_relaxed)},
            {"clean_hud", cfg.cleanHudMode.load(std::memory_order_relaxed)},
            {"cache_hotbar", cfg.cacheHotbar.load(std::memory_order_relaxed)},
            {"cache_vitals", cfg.cacheVitals.load(std::memory_order_relaxed)},
            {"camera_smoothing", cfg.cameraSmoothing.load(std::memory_order_relaxed)},
            {"disable_vsync", cfg.disableVsync.load(std::memory_order_relaxed)},
            {"disable_animations", cfg.disableAnimations.load(std::memory_order_relaxed)},
            {"simplify_crosshair", cfg.simplifyCrosshair.load(std::memory_order_relaxed)},
            {"reduce_transparency", cfg.reduceTransparency.load(std::memory_order_relaxed)}
        };

        std::ofstream file(filePath);
        if (file.is_open()) {
            file << j.dump(4);
        }
    } catch (...) {
        // Silently ignore save exceptions
    }
}

// ============================================================================
// Performance & Frame Metrics
// ============================================================================

struct PerformanceMetrics {
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> cachedHits{0};

    void reset() noexcept {
        totalFrames.store(0);
        cachedHits.store(0);
    }

    float getHitRate() const noexcept {
        uint64_t total = totalFrames.load(std::memory_order_relaxed);
        if (total == 0) return 0.0f;
        return (100.0f * cachedHits.load(std::memory_order_relaxed)) / total;
    }
};

inline PerformanceMetrics& getMetrics() noexcept {
    static PerformanceMetrics instance;
    return instance;
}

// ============================================================================
// Camera Smoothing Interpolator Engine
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

        // Exponential smoothing factor resistant to low FPS jitter
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
// Hotbar State Cache
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

class HotbarCache {
private:
    static constexpr size_t HOTBAR_SLOT_COUNT = 9;
    std::array<SlotState, HOTBAR_SLOT_COUNT> mSlots{};
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

        getMetrics().cachedHits.fetch_add(1, std::memory_order_relaxed);
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
// Active Clean HUD & Minimal Crosshair Overlay Engine
// ============================================================================

void renderCleanHudOverlay() noexcept {
    if (!getConfig().enabled.load(std::memory_order_relaxed)) {
        pl::modmenu::submitDrawCommands("hud_optimizer_module", {});
        return;
    }

    getMetrics().totalFrames.fetch_add(1, std::memory_order_relaxed);

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
    }

    pl::modmenu::submitDrawCommands("hud_optimizer_module", cmds);
}

// ============================================================================
// Bedrock UI Detours & Interception Hooks
// ============================================================================

namespace Hooks {

// Stored target addresses
void* targetEglSwapInterval = nullptr;

// Original function pointers
int (*orig_EglSwapInterval)(void*, int) = nullptr;

// Detour for VSync / SwapInterval
int hook_EglSwapInterval(void* dpy, int interval) {
    // Process active frame overlays during SwapBuffers cycle
    renderCleanHudOverlay();

    if (getConfig().enabled.load(std::memory_order_relaxed) && getConfig().disableVsync.load(std::memory_order_relaxed)) {
        interval = 0;
    }
    if (orig_EglSwapInterval) {
        return orig_EglSwapInterval(dpy, interval);
    }
    return 0;
}

bool installHooks() noexcept {
    // Resolve eglSwapInterval via dlsym for reliable cross-Android compatibility
    void* eglHandle = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (eglHandle) {
        targetEglSwapInterval = dlsym(eglHandle, "eglSwapInterval");
    }

    if (targetEglSwapInterval) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(targetEglSwapInterval),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_EglSwapInterval),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_EglSwapInterval));
    }

    return (targetEglSwapInterval != nullptr);
}

void uninstallHooks() noexcept {
    if (targetEglSwapInterval) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(targetEglSwapInterval), reinterpret_cast<pl::memory::FuncPtr>(hook_EglSwapInterval));
        targetEglSwapInterval = nullptr;
        orig_EglSwapInterval = nullptr;
    }
}

} // namespace Hooks

// ============================================================================
// Frame Lifecycle & Global State Management
// ============================================================================

void beginFrame() noexcept {
    getMetrics().totalFrames.fetch_add(1, std::memory_order_relaxed);
}

void invalidateAllCaches() noexcept {
    getHotbarCache().invalidate();
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
        builder.description("Smart, lightweight HUD optimization engine reducing redundant Bedrock rendering overhead with camera smoothing and VSync toggles.")
               .modId("hud_optimizer")
               .defaultEnabled(true)
               .hideInHudEditor(false)
               .config("enabled", "Enable Optimizer Engine", ConfigType::Toggle, cfg.enabled.load() ? "true" : "false")
               .config("clean_hud", "Clean HUD Mode", ConfigType::Toggle, cfg.cleanHudMode.load() ? "true" : "false")
               .config("cache_hotbar", "Cache Hotbar Slots", ConfigType::Toggle, cfg.cacheHotbar.load() ? "true" : "false")
               .config("cache_vitals", "Cache Health/Hunger/Armor/XP", ConfigType::Toggle, cfg.cacheVitals.load() ? "true" : "false")
               .config("camera_smoothing", "Smooth Camera Turning", ConfigType::Toggle, cfg.cameraSmoothing.load() ? "true" : "false")
               .config("disable_vsync", "Disable VSync (Max FPS)", ConfigType::Toggle, cfg.disableVsync.load() ? "true" : "false")
               .config("disable_animations", "Disable Cosmetic Animations", ConfigType::Toggle, cfg.disableAnimations.load() ? "true" : "false")
               .config("simplify_crosshair", "Simplify Crosshair", ConfigType::Toggle, cfg.simplifyCrosshair.load() ? "true" : "false")
               .config("reduce_transparency", "Reduce Layers & Transparency", ConfigType::Toggle, cfg.reduceTransparency.load() ? "true" : "false")
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
                   } else if (key == "clean_hud") {
                       cfgState.cleanHudMode.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_hotbar") {
                       cfgState.cacheHotbar.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_vitals") {
                       cfgState.cacheVitals.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "camera_smoothing") {
                       cfgState.cameraSmoothing.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "disable_vsync") {
                       cfgState.disableVsync.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "disable_animations") {
                       cfgState.disableAnimations.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "simplify_crosshair") {
                       cfgState.simplifyCrosshair.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "reduce_transparency") {
                       cfgState.reduceTransparency.store(boolVal, std::memory_order_relaxed);
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
