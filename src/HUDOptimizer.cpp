// HUDOptimizer.cpp - Diagnostic Build (ModMenu Bypassed)
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <mutex>

#include <pl/Mod.hpp>

namespace HUDOptimizer {

struct PerformanceMetrics {
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> cachedHits{0};
    
    float getHitRate() const noexcept {
        uint64_t total = totalFrames.load();
        if (total == 0) return 0.0f;
        return (100.0f * cachedHits.load()) / total;
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

    std::unordered_map<std::string, HudElement> elements;
    std::mutex stateMutex;
    uint64_t currentFrame = 0;
    
public:
    void beginFrame() noexcept {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentFrame++;
    }

    bool shouldUpdateElement(const char *name, int newValue) noexcept {
        if (!name) return true;
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
    }

    void invalidateAll() noexcept {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (auto &pair : elements) pair.second.dirty = true;
    }
};

inline HudStateTracker& getStateTracker() noexcept {
    static HudStateTracker tracker;
    return tracker;
}

struct Config {
    std::atomic<bool> enabled{true};
};

inline Config& getConfig() noexcept {
    static Config config;
    return config;
}

bool shouldUpdateHealth(int health) noexcept {
    if (!getConfig().enabled.load()) return true;
    return getStateTracker().shouldUpdateElement("health", health);
}

void frameBegin() noexcept {
    getStateTracker().beginFrame();
}

void invalidateAll() noexcept {
    getStateTracker().invalidateAll();
}

} // namespace HUDOptimizer

class HUDOptimizerMod {
private:
    ll::mod::NativeMod& mSelf;

public:
    static HUDOptimizerMod &instance() noexcept {
        static HUDOptimizerMod mod;
        return mod;
    }

    HUDOptimizerMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() noexcept {
        return true;
    }

    bool enable() noexcept {
        // Bypassing pl::modmenu entirely to test core runtime stability on Android 16
        mSelf.getLogger().info("HUD Optimizer core loaded successfully without ModMenu UI.");
        return true;
    }

    bool disable() noexcept {
        HUDOptimizer::getConfig().enabled.store(false);
        HUDOptimizer::invalidateAll();
        return true;
    }

    bool unload() noexcept {
        return true;
    }
};

PL_REGISTER_MOD(
    HUDOptimizerMod,
    HUDOptimizerMod::instance()
);
