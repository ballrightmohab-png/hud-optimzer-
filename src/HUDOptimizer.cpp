// HUDOptimizer.cpp
//
// HUD Optimizer Pro - Smart State Caching (Crash-Proof Version for Android/LeviMC)
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
    
    float getHitRate() const noexcept {
        try {
            uint64_t total = totalFrames.load();
            if (total == 0) return 0.0f;
            return (100.0f * cachedHits.load()) / total;
        } catch (...) {
            return 0.0f;
        }
    }
};

inline PerformanceMetrics& getMetrics() noexcept {
    static PerformanceMetrics metrics;
    return metrics;
}

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
    void beginFrame() noexcept {
        try {
            std::lock_guard<std::mutex> lock(stateMutex);
            currentFrame++;
        } catch (...) {}
    }

    bool shouldUpdateElement(const char *name, int newValue) noexcept {
        if (!name) return true;
        try {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = elements.find(name);
            if (it == elements.end()) {
                elements[name] = {newValue, true, currentFrame};
                return true;
            }
            HudElement &elem = it->second;
            if (elem.value == newValue && !elem.dirty) {
                getMetrics().cachedHits++;
                return false;
            }
            elem.value = newValue;
            elem.dirty = false;
            elem.lastUpdateFrame = currentFrame;
            return true;
        } catch (...) {
            return true;
        }
    }

    bool shouldUpdateContainer(const char *name, uint64_t revision) noexcept {
        if (!name) return true;
        try {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = containers.find(name);
            if (it == containers.end()) {
                containers[name] = {revision, true};
                return true;
            }
            ContainerState &state = it->second;
            if (state.revision == revision && !state.dirty) {
                getMetrics().cachedHits++;
                return false;
            }
            state.revision = revision;
            state.dirty = false;
            return true;
        } catch (...) {
            return true;
        }
    }

    void invalidateAll() noexcept {
        try {
            std::lock_guard<std::mutex> lock(stateMutex);
            for (auto &pair : elements) pair.second.dirty = true;
            for (auto &pair : containers) pair.second.dirty = true;
        } catch (...) {}
    }
};

// Meyers Singleton: prevents static initialization heap crashes during dlopen
inline HudStateTracker& getStateTracker() noexcept {
    static HudStateTracker tracker;
    return tracker;
}

struct Config {
    std::atomic<bool> enabled{true};
    std::atomic<bool> aggressiveMode{false};
    std::atomic<bool> showStats{false};
};

inline Config& getConfig() noexcept {
    static Config config;
    return config;
}

bool shouldUpdateHotbar(uint64_t inventoryRevision) noexcept {
    if (!getConfig().enabled.load()) return true;
    getMetrics().totalFrames++;
    return getStateTracker().shouldUpdateContainer("hotbar", inventoryRevision);
}

bool shouldUpdateHealth(int health) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("health", health);
}

bool shouldUpdateArmor(int armor) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("armor", armor);
}

bool shouldUpdateHunger(int hunger) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("hunger", hunger);
}

bool shouldUpdateXP(int xp) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("xp", xp);
}

bool shouldUpdateHotbarSelection(int selectedSlot) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("selectedSlot", selectedSlot);
}

bool shouldUpdateCrosshair(bool visible) noexcept {
    if (!getConfig().enabled.load()) return true;
    static thread_local int lastVisible = -1;
    int newVal = visible ? 1 : 0;
    if (lastVisible == newVal) {
        getMetrics().cachedHits++;
        return false;
    }
    lastVisible = newVal;
    return true;
}

bool shouldUpdateStatus(int statusMask) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("status", statusMask);
}

void frameBegin() noexcept {
    getStateTracker().beginFrame();
}

void invalidateAll() noexcept {
    getStateTracker().invalidateAll();
}

std::string getStats() {
    try {
        std::string stats = "HUD Optimizer Stats:\n";
        stats += "  Frames: " + std::to_string(getMetrics().totalFrames.load()) + "\n";
        stats += "  Cache Hit Rate: " + std::to_string(getMetrics().getHitRate()) + "%\n";
        stats += "  Status: " + std::string(getConfig().enabled.load() ? "ENABLED" : "DISABLED") + "\n";
        return stats;
    } catch (...) {
        return "HUD Optimizer Stats: Unavailable";
    }
}

static void onToggleOptimizer(std::string_view, bool enabled) noexcept {
    getConfig().enabled.store(enabled);
    invalidateAll();
}

static void onToggleAggressiveMode(std::string_view, bool enabled) noexcept {
    getConfig().aggressiveMode.store(enabled);
    invalidateAll();
}

static void onToggleStats(std::string_view, bool enabled) noexcept {
    getConfig().showStats.store(enabled);
}

} // namespace HUDOptimizer

class HUDOptimizerMod {
public:
    static HUDOptimizerMod &instance() noexcept {
        static HUDOptimizerMod mod;
        return mod;
    }

    HUDOptimizerMod() = default;

    [[nodiscard]]
    ll::mod::NativeMod* getModPtr() const noexcept {
        try {
            return ll::mod::NativeMod::current();
        } catch (...) {
            return nullptr;
        }
    }

    bool load() noexcept {
        return true;
    }

    bool enable() noexcept {
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
                    .onConfigChanged(&HUDOptimizerMod::onConfigChanged)
                    .registerModule();
            } catch (...) {
                registered = false;
            }

            if (!registered && mod) {
                try {
                    mod->getLogger().warn("ModMenu registration deferred; core caching active.");
                } catch (...) {}
            }

            return true;
        } catch (...) {
            return false;
        }
    }

    bool disable() noexcept {
        try {
            HUDOptimizer::getConfig().enabled.store(false);
            HUDOptimizer::invalidateAll();
            try {
                pl::modmenu::unregisterModule("hud_optimizer.main");
            } catch (...) {}
            return true;
        } catch (...) {
            return false;
        }
    }

    bool unload() noexcept {
        try {
            HUDOptimizer::getConfig().enabled.store(false);
            HUDOptimizer::invalidateAll();
            return true;
        } catch (...) {
            return false;
        }
    }

    static void onConfigChanged(
        std::string_view moduleId,
        std::string_view key,
        std::string_view value
    ) noexcept {
        try {
            std::string text(value);
            char *end = nullptr;
            const long parsed = std::strtol(text.c_str(), &end, 10);
            if (end == text.c_str()) return;

            if (key == "aggressive_mode") {
                const int mode = static_cast<int>(std::clamp<long>(parsed, 0, 1));
                HUDOptimizer::getConfig().aggressiveMode.store(mode == 1);
            } else if (key == "show_stats") {
                const int show = static_cast<int>(std::clamp<long>(parsed, 0, 1));
                HUDOptimizer::getConfig().showStats.store(show == 1);
            }
        } catch (...) {}
    }
};

PL_REGISTER_MOD(
    HUDOptimizerMod,
    HUDOptimizerMod::instance()
);
