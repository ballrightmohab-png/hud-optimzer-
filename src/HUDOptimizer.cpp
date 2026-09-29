// HUDOptimizer.cpp - Camera Smoothing + TFR + HUD Optimization Module for Minecraft Bedrock on LeviLaunchroid
// Architecture: Native C++20 module targeting Android ARM64

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include "CameraSmoothing.hpp"
#include "TFREngine.hpp"

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

    // Camera Smoothing Controls
    std::atomic<bool> cameraSmoothingEnabled{true};
    std::atomic<float> cameraSmoothness{12.0f};

    // TFR Frame Generation Controls
    std::atomic<bool> tfrEnabled{true};
    std::atomic<TFR::TFRMode> tfrMode{TFR::TFRMode::AUTO};
    std::atomic<bool> preserveHUDInTFR{true};
};

inline Config& getConfig() noexcept {
    static Config instance;
    return instance;
}

// ============================================================================
// Performance & Frame Metrics
// ============================================================================

struct PerformanceMetrics {
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> generatedFrames{0};
    std::atomic<uint64_t> cachedHits{0};
    std::atomic<uint64_t> hotbarBypasses{0};
    std::atomic<uint64_t> vitalBypasses{0};

    void reset() noexcept {
        totalFrames.store(0);
        generatedFrames.store(0);
        cachedHits.store(0);
        hotbarBypasses.store(0);
        vitalBypasses.store(0);
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
// Hotbar State Cache
// ============================================================================

struct SlotState {
    int itemId{0};
    int count{0};
    int damage{0};
    uint32_t customFlags{0};

    bool operator==(const SlotState& other) const noexcept {
        return itemId == other.itemId &&
               count == other.count &&
               damage == other.damage &&
               customFlags == other.customFlags;
    }

    bool operator!=(const SlotState& other) const noexcept {
        return !(*this == other);
    }
};

class HotbarCache {
private:
    static constexpr size_t HOTBAR_SLOT_COUNT = 9;
    std::array<SlotState, HOTBAR_SLOT_COUNT> mSlots{};
    int mSelectedSlot{-1};
    bool mDirty{true};
    mutable std::mutex mMutex;

public:
    void invalidate() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mDirty = true;
    }

    bool isSlotDirty(size_t index, const SlotState& newState) noexcept {
        if (!getConfig().cacheHotbar.load(std::memory_order_relaxed)) {
            return true;
        }

        if (index >= HOTBAR_SLOT_COUNT) return true;

        std::lock_guard<std::mutex> lock(mMutex);
        if (mDirty || mSlots[index] != newState) {
            mSlots[index] = newState;
            return true;
        }

        getMetrics().hotbarBypasses.fetch_add(1, std::memory_order_relaxed);
        getMetrics().cachedHits.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool isSelectionDirty(int newSelectedSlot) noexcept {
        if (!getConfig().cacheHotbar.load(std::memory_order_relaxed)) {
            return true;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (mDirty || mSelectedSlot != newSelectedSlot) {
            mSelectedSlot = newSelectedSlot;
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
// Vitals State Cache (Health, Hunger, Armor, XP Level & Progress)
// ============================================================================

struct VitalsState {
    int health{-1};
    int maxHealth{-1};
    int hunger{-1};
    int armor{-1};
    int xpLevel{-1};
    float xpProgress{-1.0f};

    bool operator==(const VitalsState& other) const noexcept {
        return health == other.health &&
               maxHealth == other.maxHealth &&
               hunger == other.hunger &&
               armor == other.armor &&
               xpLevel == other.xpLevel &&
               xpProgress == other.xpProgress;
    }

    bool operator!=(const VitalsState& other) const noexcept {
        return !(*this == other);
    }
};

class VitalsCache {
private:
    VitalsState mState{};
    bool mDirty{true};
    mutable std::mutex mMutex;

public:
    void invalidate() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mDirty = true;
    }

    bool shouldRedraw(const VitalsState& newState) noexcept {
        if (!getConfig().cacheVitals.load(std::memory_order_relaxed)) {
            return true;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (mDirty || mState != newState) {
            mState = newState;
            mDirty = false;
            return true;
        }

        getMetrics().vitalBypasses.fetch_add(1, std::memory_order_relaxed);
        getMetrics().cachedHits.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
};

inline VitalsCache& getVitalsCache() noexcept {
    static VitalsCache instance;
    return instance;
}

// ============================================================================
// Bedrock Native Detours & Interception Hooks
// ============================================================================

namespace Hooks {

// Original function pointers
void (*orig_HudElementRender)(void*, void*) = nullptr;
void (*orig_HotbarRenderSlot)(void*, int, void*) = nullptr;
void (*orig_MouseRawInput)(void*, float, float) = nullptr;
void (*orig_RenderWorld)(void*, float) = nullptr;

// Detour for raw input updates -> Target Camera rotation immediately updated with zero latency
void hook_MouseRawInput(void* self, float deltaX, float deltaY) {
    if (getConfig().cameraSmoothingEnabled.load(std::memory_order_relaxed)) {
        // Raw mouse/touch movement directly feeds target camera
        CameraSystem::getCameraEngine().updateRawInputDelta(deltaX, deltaY);
    }

    if (orig_MouseRawInput) {
        orig_MouseRawInput(self, deltaX, deltaY);
    }
}

// Detour for general HUD element rendering
void hook_HudElementRender(void* self, void* ctx) {
    if (!getConfig().enabled.load(std::memory_order_relaxed)) {
        if (orig_HudElementRender) orig_HudElementRender(self, ctx);
        return;
    }

    getMetrics().totalFrames.fetch_add(1, std::memory_order_relaxed);

    if (orig_HudElementRender) {
        orig_HudElementRender(self, ctx);
    }

    getHotbarCache().finishFrame();
}

// Detour for individual Hotbar Slot rendering
void hook_HotbarRenderSlot(void* self, int slotIndex, void* slotData) {
    if (!getConfig().enabled.load(std::memory_order_relaxed) || !getConfig().cacheHotbar.load(std::memory_order_relaxed)) {
        if (orig_HotbarRenderSlot) orig_HotbarRenderSlot(self, slotIndex, slotData);
        return;
    }

    SlotState currentSlotState{};
    if (slotData) {
        std::memcpy(&currentSlotState, slotData, std::min(sizeof(SlotState), sizeof(uint64_t) * 2));
    }

    if (getHotbarCache().isSlotDirty(static_cast<size_t>(slotIndex), currentSlotState)) {
        if (orig_HotbarRenderSlot) orig_HotbarRenderSlot(self, slotIndex, slotData);
    }
}

// Detour for World Rendering pass: Integrates Camera Smoothing and TFR Temporal Generation
void hook_RenderWorld(void* self, float dt) {
    // 1. Update Camera Smoothing and apply to visual camera context
    CameraSystem::CameraSmoothingEngine::State camState{};
    if (getConfig().cameraSmoothingEnabled.load(std::memory_order_relaxed)) {
        camState = CameraSystem::getCameraEngine().updateVisualCamera(dt);
        CameraSystem::getCameraEngine().applyToCameraContext(self, camState.renderedYaw, camState.renderedPitch);
    }

    // 2. TFR Temporal Frame Recording & Generation pipeline
    if (getConfig().tfrEnabled.load(std::memory_order_relaxed)) {
        TFR::FrameMetadata frameMeta{};
        frameMeta.frameIndex = getMetrics().totalFrames.load(std::memory_order_relaxed);
        frameMeta.cameraYaw = camState.renderedYaw;
        frameMeta.cameraPitch = camState.renderedPitch;

        TFR::getTFREngine().recordRealFrame(frameMeta, dt);

        if (TFR::getTFREngine().shouldGenerateFrame()) {
            // Reconstruct and present intermediate frame G0.5 to render pipeline
            TFR::getTFREngine().renderAndPresentGeneratedFrame(self, 0.5f);
            getMetrics().generatedFrames.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (orig_RenderWorld) {
        orig_RenderWorld(self, dt);
    }
}

bool installHooks() noexcept {
    uintptr_t hudRenderAddr = pl::memory::resolveSignature("48 89 5C 24 ?? 57 48 83 EC ?? 48 8B D9", "libminecraftpe.so");
    uintptr_t hotbarSlotAddr = pl::memory::resolveSignature("40 53 48 83 EC ?? 48 8B D9 89 54 24", "libminecraftpe.so");
    uintptr_t rawInputAddr = pl::memory::resolveSignature("0F 29 74 24 ?? 0F 28 F2 48 8B F1", "libminecraftpe.so");
    uintptr_t renderWorldAddr = pl::memory::resolveSignature("48 8B C4 48 89 58 08 48 89 6C 24 10", "libminecraftpe.so");

    bool success = true;
    if (hudRenderAddr) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(hudRenderAddr),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_HudElementRender),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_HudElementRender));
    } else success = false;

    if (hotbarSlotAddr) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(hotbarSlotAddr),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_HotbarRenderSlot),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_HotbarRenderSlot));
    } else success = false;

    if (rawInputAddr) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(rawInputAddr),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_MouseRawInput),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_MouseRawInput));
    }

    if (renderWorldAddr) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(renderWorldAddr),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_RenderWorld),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_RenderWorld));
    }

    return success;
}

void uninstallHooks() noexcept {
    if (orig_HudElementRender) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(hook_HudElementRender), reinterpret_cast<pl::memory::FuncPtr>(hook_HudElementRender));
        orig_HudElementRender = nullptr;
    }
    if (orig_HotbarRenderSlot) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(hook_HotbarRenderSlot), reinterpret_cast<pl::memory::FuncPtr>(hook_HotbarRenderSlot));
        orig_HotbarRenderSlot = nullptr;
    }
    if (orig_MouseRawInput) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(hook_MouseRawInput), reinterpret_cast<pl::memory::FuncPtr>(hook_MouseRawInput));
        orig_MouseRawInput = nullptr;
    }
    if (orig_RenderWorld) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(hook_RenderWorld), reinterpret_cast<pl::memory::FuncPtr>(hook_RenderWorld));
        orig_RenderWorld = nullptr;
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
    getVitalsCache().invalidate();
}

static void handleToggleCallback(std::string_view moduleId, bool enabled) {
    getConfig().enabled.store(enabled, std::memory_order_relaxed);
    invalidateAllCaches();
}

static void handleConfigChangedCallback(std::string_view moduleId, std::string_view key, std::string_view value) {
    auto& cfg = getConfig();
    bool boolVal = (value == "true" || value == "1");

    if (key == "clean_hud") {
        cfg.cleanHudMode.store(boolVal, std::memory_order_relaxed);
    } else if (key == "cache_hotbar") {
        cfg.cacheHotbar.store(boolVal, std::memory_order_relaxed);
    } else if (key == "cache_vitals") {
        cfg.cacheVitals.store(boolVal, std::memory_order_relaxed);
    } else if (key == "camera_smoothing") {
        cfg.cameraSmoothingEnabled.store(boolVal, std::memory_order_relaxed);
        CameraSystem::getCameraEngine().setEnabled(boolVal);
    } else if (key == "camera_smoothness") {
        std::string valStr(value);
        try {
            float smoothness = std::stof(valStr);
            cfg.cameraSmoothness.store(smoothness, std::memory_order_relaxed);
            CameraSystem::getCameraEngine().setSmoothness(smoothness);
        } catch (...) {}
    } else if (key == "tfr_enabled") {
        cfg.tfrEnabled.store(boolVal, std::memory_order_relaxed);
        TFR::getTFREngine().setEnabled(boolVal);
    } else if (key == "tfr_mode") {
        std::string valStr(value);
        try {
            int modeVal = std::stoi(valStr);
            TFR::TFRMode mode = static_cast<TFR::TFRMode>(modeVal);
            cfg.tfrMode.store(mode, std::memory_order_relaxed);
            TFR::getTFREngine().setMode(mode);
        } catch (...) {}
    } else if (key == "preserve_hud_tfr") {
        cfg.preserveHUDInTFR.store(boolVal, std::memory_order_relaxed);
        TFR::getTFREngine().setHUDPreservation(boolVal);
    }

    invalidateAllCaches();
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

        ModuleBuilder builder("hud_optimizer_module", "HUD Optimizer & TFR Engine");
        builder.description("Smart HUD optimization, Camera Smoothing, and Temporal Frame Reconstruction (TFR) engine.")
               .modId("hud_optimizer")
               .defaultEnabled(true)
               .hideInHudEditor(false)
               .config("clean_hud", "Clean HUD Mode", ConfigType::Toggle, "false")
               .config("cache_hotbar", "Cache Hotbar Slots", ConfigType::Toggle, "true")
               .config("cache_vitals", "Cache Health/Hunger/Armor/XP", ConfigType::Toggle, "true")
               .config("camera_smoothing", "Enable Camera Smoothing", ConfigType::Toggle, "true")
               .config("camera_smoothness", "Camera Smoothness Factor", ConfigType::Slider, "12.0")
               .config("tfr_enabled", "Enable Temporal Frame Reconstruction (TFR)", ConfigType::Toggle, "true")
               .config("tfr_mode", "TFR Mode (0=Off, 1=1x, 2=2x, 3=Auto)", ConfigType::Slider, "3")
               .config("preserve_hud_tfr", "Preserve HUD Crispness in TFR", ConfigType::Toggle, "true")
               .onToggle(&HUDOptimizer::handleToggleCallback)
               .onConfigChanged(&HUDOptimizer::handleConfigChangedCallback);

        if (builder.registerModule()) {
            mSelf.getLogger().info("HUD Optimizer & TFR Engine ModMenu module registered successfully.");
        } else {
            mSelf.getLogger().warn("Failed to register HUD Optimizer ModMenu module.");
        }
    }

public:
    static HUDOptimizerMod& instance() noexcept {
        static HUDOptimizerMod mod;
        return mod;
    }

    HUDOptimizerMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() noexcept {
        mSelf.getLogger().info("Loading HUD Optimizer & TFR Native Engine...");
        return true;
    }

    bool enable() noexcept {
        mSelf.getLogger().info("Enabling HUD Optimizer & TFR Engine...");
        HUDOptimizer::getConfig().enabled.store(true, std::memory_order_relaxed);
        HUDOptimizer::invalidateAllCaches();

        bool hooksInstalled = HUDOptimizer::Hooks::installHooks();
        if (hooksInstalled) {
            mSelf.getLogger().info("Native HUD & Render interception hooks installed successfully.");
        } else {
            mSelf.getLogger().warn("Native HUD hooks partially installed; operating in safe mode.");
        }

        registerModMenuControls();
        mSelf.getLogger().info("HUD Optimizer & TFR Engine enabled successfully!");
        return true;
    }

    bool disable() noexcept {
        mSelf.getLogger().info("Disabling HUD Optimizer & TFR Engine...");
        HUDOptimizer::getConfig().enabled.store(false, std::memory_order_relaxed);
        HUDOptimizer::invalidateAllCaches();
        HUDOptimizer::Hooks::uninstallHooks();
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
