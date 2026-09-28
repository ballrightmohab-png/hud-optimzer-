// HUDOptimizer.cpp
//
// HUD Optimizer Pro - Smart State Caching (Crash-Proof Version)
//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <stdexcept>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace HUDOptimizer {

struct PerformanceMetrics {
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> skippedUpdates{0};
    std::atomic<uint64_t> cachedHits{0};
    
    float getHitRate() const {
        uint64_t total = totalFrames.load();
        if (total == 0) return 0.0f;
        return (100.0f * cachedHits.load()) / total;
    }
};

static PerformanceMetrics gMetrics;

class HudStateTracker {
private:
    struct HudElement {
        int value = -1;
        bool dirty = true;
        uint64_t lastUpdateFrame = 0;
    };

    struct ContainerState {
        uint64_t revision = 0;
        bool dirty = true;
    };

    std::unordered_map<std::string, HudElement> elements;
    std::unordered_map<std::string, ContainerState> containers;
    std::mutex stateMutex;
    uint64_t currentFrame = 0;
    
public:
    void beginFrame() {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentFrame++;
    }

    bool shouldUpdateElement(const char *name, int newValue) {
        if (!name) return true;
        std::lock_guard<std::mutex> lock(stateMutex);
        auto it = elements.find(name);
        if (it == elements.end()) {
            elements[name] = {newValue, true, currentFrame};
            return true;
        }
        HudElement &elem = it->second;
        if (elem.value == newValue && !elem.dirty) {
            gMetrics.cachedHits++;
            return false;
        }
        elem.value = newValue;
        elem.dirty = false;
        elem.lastUpdateFrame = currentFrame;
        return true;
    }

    bool shouldUpdateContainer(const char *name, uint64_t revision) {
        if (!name) return true;
        std::lock_guard<std::mutex> lock(stateMutex);
        auto it = containers.find(name);
        if (it == containers.end()) {
            containers[name] = {revision, true};
            return true;
        }
        ContainerState &state = it->second;
        if (state.revision == revision && !state.dirty) {
            gMetrics.cachedHits++;
            return false;
        }
        state.revision = revision;
        state.dirty = false;
        return true;
    }

    void invalidateAll() {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (auto &pair : elements) pair.second.dirty = true;
        for (auto &pair : containers) pair.second.dirty = true;
    }
};

static HudStateTracker gStateTracker;

struct Config {
    std::atomic<bool> enabled{true};
    std::atomic<bool> aggressiveMode{false};
    std::atomic<bool> showStats{false};
};

static Config gConfig;

// ============================================================
// PUBLIC OPTIMIZATION HOOKS
// ============================================================

bool shouldUpdateHotbar(uint64_t inventoryRevision) {
    if (!gConfig.enabled.load()) return true;
    gMetrics.totalFrames++;
    return gStateTracker.shouldUpdateContainer("hotbar", inventoryRevision);
}

bool shouldUpdateHealth(int health) {
    if (!gConfig.enabled.load()) return true;
    return gStateTracker.shouldUpdateElement("health", health);
}

bool shouldUpdateArmor(int armor) {
    if (!gConfig.enabled.load()) return true;
    return gStateTracker.shouldUpdateElement("armor", armor);
}

bool shouldUpdateHunger(int hunger) {
    if (!gConfig.enabled.load()) return true;
    return gStateTracker.shouldUpdateElement("hunger", hunger);
}

bool shouldUpdateXP(int xp) {
    if (!gConfig.enabled.load()) return true;
    return gStateTracker.shouldUpdateElement("xp", xp);
}

bool shouldUpdateHotbarSelection(int selectedSlot) {
    if (!gConfig.enabled.load()) return true;
    return gStateTracker.shouldUpdateElement("selectedSlot", selectedSlot);
}

bool shouldUpdateCrosshair(bool visible) {
    if (!gConfig.enabled.load()) return true;
    static int lastVisible = -1;
    int newVal = visible ? 1 : 0;
    if (lastVisible == newVal) {
        gMetrics.cachedHits++;
        return false;
    }
    lastVisible = newVal;
    return true;
}

bool shouldUpdateStatus(int statusMask) {
    if (!gConfig.enabled.load()) return true;
    return gStateTracker.shouldUpdateElement("status", statusMask);
}

void frameBegin() {
    gStateTracker.beginFrame();
}

void invalidateAll() {
    gStateTracker.invalidateAll();
}

std::string getStats() {
    try {
        std::string stats = "HUD Optimizer Stats:\n";
        stats += "  Frames: " + std::to_string(gMetrics.totalFrames.load()) + "\n";
        stats += "  Cache Hit Rate: " + std::to_string(gMetrics.getHitRate()) + "%\n";
        stats += "  Status: " + std::string(gConfig.enabled.load() ? "ENABLED" : "DISABLED") + "\n";
        return stats;
    } catch (...) {
        return "HUD Optimizer Stats: Unavailable";
    }
}

static void onToggleOptimizer(std::string_view, bool enabled) {
    gConfig.enabled.store(enabled);
    invalidateAll();
}

static void onToggleAggressiveMode(std::string_view, bool enabled) {
    gConfig.aggressiveMode.store(enabled);
    invalidateAll();
}

static void onToggleStats(std::string_view, bool enabled) {
    gConfig.showStats.store(enabled);
}

} // namespace HUDOptimizer

// ============================================================
// HUD OPTIMIZER MOD - Launcher Integration
// ============================================================

class HUDOptimizerMod {
public:
    static HUDOptimizerMod &instance() {
        static HUDOptimizerMod mod;
        return mod;
    }

    HUDOptimizerMod() = default;

    [[nodiscard]]
    ll::mod::NativeMod* getModPtr() const {
        try {
            return ll::mod::NativeMod::current();
        } catch (...) {
            return nullptr;
        }
    }

    bool load() {
        return true;
    }

    bool enable() {
        try {
            auto* mod = getModPtr();
            std::string modId = "hud_optimizer.main";
            
            if (mod) {
                try {
                    std::string id = mod->getId();
                    if (!id.empty()) {
                        modId = id + ".hud_optimizer";
                    }
                } catch (...) {}
            }

            // Safely attempt mod menu registration with error boundaries
            bool registered = false;
            try {
                registered = 
                    pl::modmenu::ModuleBuilder(
                        modId,
                        "HUD Optimizer"
                    )
                    .description(
                        "⚡ Smart HUD rendering optimizer\n"
                        "Reduces redundant draw calls by caching unchanged HUD state."
                    )
                    .defaultEnabled(true)
                    .onToggle(HUDOptimizer::onToggleOptimizer)
                    .config(
                        "aggressive_mode",
                        "Aggressive Caching: 0=OFF | 1=ON",
                        pl::modmenu::ConfigType::SliderInt,
                        "0", "0", "1"
                    )
                    .config(
                        "show_stats",
                        "Show Cache Stats: 0=OFF | 1=ON",
                        pl::modmenu::ConfigType::SliderInt,
                        "0", "0", "1"
                    )
                    .onConfigChanged(onConfigChanged)
                    .registerModule();
            } catch (...) {
                registered = false;
            }

            if (!registered && mod) {
                try {
                    mod->getLogger().warn("ModMenu registration bypassed; running core hooks safely.");
                } catch (...) {}
            }

            return true;
        } catch (...) {
            // Absolute safety net against any unexpected native startup exceptions
            return false;
        }
    }

    bool disable() {
        try {
            HUDOptimizer::gConfig.enabled.store(false);
            HUDOptimizer::invalidateAll();
            try {
                pl::modmenu::unregisterModule("hud_optimizer.main");
            } catch (...) {}
            return true;
        } catch (...) {
            return false;
        }
    }

    bool unload() {
        try {
            HUDOptimizer::gConfig.enabled.store(false);
            HUDOptimizer::invalidateAll();
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    static void onConfigChanged(
        std::string_view moduleId,
        std::string_view key,
        std::string_view value
    ) {
        try {
            std::string text(value);
            char *end = nullptr;
            const long parsed = std::strtol(text.c_str(), &end, 10);
            if (end == text.c_str()) return;

            if (key == "aggressive_mode") {
                const int mode = static_cast<int>(std::clamp<long>(parsed, 0, 1));
                HUDOptimizer::gConfig.aggressiveMode.store(mode == 1);
            } else if (key == "show_stats") {
                const int show = static_cast<int>(std::clamp<long>(parsed, 0, 1));
                HUDOptimizer::gConfig.showStats.store(show == 1);
            }
        } catch (...) {}
    }
};

PL_REGISTER_MOD(
    HUDOptimizerMod,
    HUDOptimizerMod::instance()
);
