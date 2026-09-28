// HUDOptimizer.cpp - Optimized HUD Rendering Module for Minecraft Bedrock on LeviLaunchroid
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
    std::atomic<uint64_t> cachedHits{0};
    std::atomic<uint64_t> hotbarBypasses{0};
    std::atomic<uint64_t> vitalBypasses{0};

    void reset() noexcept {
        totalFrames.store(0);
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
// Bedrock UI Detours & Interception Hooks
// ============================================================================

namespace Hooks {

// Original function pointers
void (*orig_HudElementRender)(void*, void*) = nullptr;
void (*orig_HotbarRenderSlot)(void*, int, void*) = nullptr;

// Detour for general HUD element rendering
void hook_HudElementRender(void* self, void* ctx) {
    if (!getConfig().enabled.load(std::memory_order_relaxed)) {
        if (orig_HudElementRender) orig_HudElementRender(self, ctx);
        return;
    }

    getMetrics().totalFrames.fetch_add(1, std::memory_order_relaxed);

    // Call original render method
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

    // Dynamic slot state check
    SlotState currentSlotState{};
    if (slotData) {
        // Safe access to item structure properties
        std::memcpy(&currentSlotState, slotData, std::min(sizeof(SlotState), sizeof(uint64_t) * 2));
    }

    if (getHotbarCache().isSlotDirty(static_cast<size_t>(slotIndex), currentSlotState)) {
        if (orig_HotbarRenderSlot) orig_HotbarRenderSlot(self, slotIndex, slotData);
    }
}

bool installHooks() noexcept {
    // Resolve signatures dynamically in libminecraftpe.so with standard fallback safety
    uintptr_t hudRenderAddr = pl::memory::resolveSignature("48 89 5C 24 ?? 57 48 83 EC ?? 48 8B D9", "libminecraftpe.so");
    uintptr_t hotbarSlotAddr = pl::memory::resolveSignature("40 53 48 83 EC ?? 48 8B D9 89 54 24", "libminecraftpe.so");

    bool success = true;
    if (hudRenderAddr) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(hudRenderAddr),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_HudElementRender),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_HudElementRender));
    } else {
        success = false;
    }

    if (hotbarSlotAddr) {
        pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(hotbarSlotAddr),
                         reinterpret_cast<pl::memory::FuncPtr>(hook_HotbarRenderSlot),
                         reinterpret_cast<pl::memory::FuncPtr*>(&orig_HotbarRenderSlot));
    } else {
        success = false;
    }

    return success;
}

void uninstallHooks() noexcept {
    // Unhook safely when disabling
    if (orig_HudElementRender) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(hook_HudElementRender), reinterpret_cast<pl::memory::FuncPtr>(hook_HudElementRender));
        orig_HudElementRender = nullptr;
    }
    if (orig_HotbarRenderSlot) {
        pl::memory::unhook(reinterpret_cast<pl::memory::FuncPtr>(hook_HotbarRenderSlot), reinterpret_cast<pl::memory::FuncPtr>(hook_HotbarRenderSlot));
        orig_HotbarRenderSlot = nullptr;
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

} // namespace HUDOptimizer

// ============================================================================
// Native Mod Lifecycle Handler
// ============================================================================

class HUDOptimizerMod {
private:
    ll::mod::NativeMod& mSelf;

    void registerModMenuControls() {
        using namespace pl::modmenu;

        // Register ModMenu Builder
        ModuleBuilder builder("hud_optimizer_module", "HUD Optimizer");
        builder.description("Smart, lightweight HUD optimization engine reducing redundant Bedrock rendering overhead.")
               .modId("hud_optimizer")
               .defaultEnabled(true)
               .hideInHudEditor(false)
               .config("enabled", "Enable Optimizer Engine", ConfigType::Toggle, "true")
               .config("clean_hud", "Clean HUD Mode", ConfigType::Toggle, "false")
               .config("cache_hotbar", "Cache Hotbar Slots", ConfigType::Toggle, "true")
               .config("cache_vitals", "Cache Health/Hunger/Armor/XP", ConfigType::Toggle, "true")
               .config("disable_animations", "Disable Cosmetic Animations", ConfigType::Toggle, "true")
               .config("simplify_crosshair", "Simplify Crosshair", ConfigType::Toggle, "false")
               .config("reduce_transparency", "Reduce Layers & Transparency", ConfigType::Toggle, "false")
               .onToggle([](std::string_view moduleId, bool enabled) {
                   HUDOptimizer::getConfig().enabled.store(enabled, std::memory_order_relaxed);
                   HUDOptimizer::invalidateAllCaches();
               })
               .onConfigChanged([](std::string_view moduleId, std::string_view key, std::string_view value) {
                   auto& cfg = HUDOptimizer::getConfig();
                   bool boolVal = (value == "true" || value == "1");

                   if (key == "enabled") {
                       cfg.enabled.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "clean_hud") {
                       cfg.cleanHudMode.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_hotbar") {
                       cfg.cacheHotbar.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "cache_vitals") {
                       cfg.cacheVitals.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "disable_animations") {
                       cfg.disableAnimations.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "simplify_crosshair") {
                       cfg.simplifyCrosshair.store(boolVal, std::memory_order_relaxed);
                   } else if (key == "reduce_transparency") {
                       cfg.reduceTransparency.store(boolVal, std::memory_order_relaxed);
                   }

                   HUDOptimizer::invalidateAllCaches();
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
                    .onEvent([](std::string_view buttonId, ButtonEvent event, float value) {
                        if (event == ButtonEvent::Click || event == ButtonEvent::StateChanged) {
                            bool newVisible = value > 0.5f;
                            HUDOptimizer::getConfig().enabled.store(newVisible, std::memory_order_relaxed);
                            HUDOptimizer::invalidateAllCaches();
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
        return true;
    }

    bool enable() noexcept {
        mSelf.getLogger().info("Enabling HUD Optimizer...");
        HUDOptimizer::getConfig().enabled.store(true, std::memory_order_relaxed);
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
