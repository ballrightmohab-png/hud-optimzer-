#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <memory>

namespace HUDOptimizer {

static constexpr const char *MOD_ID = "hud_optimizer";
static constexpr const char *MODULE_ID = "hud_optimizer.main";

struct Stats {
    std::atomic<uint64_t> framesSeen{0};

    std::atomic<uint64_t> hudUpdates{0};
    std::atomic<uint64_t> hudSkips{0};

    std::atomic<uint64_t> hotbarUpdates{0};
    std::atomic<uint64_t> hotbarSkips{0};

    std::atomic<uint64_t> itemUpdates{0};
    std::atomic<uint64_t> itemSkips{0};

    std::atomic<uint64_t> healthUpdates{0};
    std::atomic<uint64_t> healthSkips{0};

    std::atomic<uint64_t> armorUpdates{0};
    std::atomic<uint64_t> armorSkips{0};

    std::atomic<uint64_t> hungerUpdates{0};
    std::atomic<uint64_t> hungerSkips{0};

    std::atomic<uint64_t> xpUpdates{0};
    std::atomic<uint64_t> xpSkips{0};

    std::atomic<uint64_t> crosshairUpdates{0};
    std::atomic<uint64_t> crosshairSkips{0};
};

static Stats gStats;

static std::atomic<bool> gEnabled{true};
static std::atomic<bool> gAggressive{false};

static std::mutex gStateMutex;

// Store module ID to prevent string lifetime issues
static const std::string MODULE_ID_STR = "hud_optimizer.main";
static const std::string MODULE_NAME = "HUD Optimizer";
static const std::string MODULE_DESC = 
    "Optimizes redundant HUD state updates "
    "while keeping the hotbar and HUD visible.";

// ============================================================
// Cached HUD state
// ============================================================

struct HudState {
    int health = -1;
    int armor = -1;
    int hunger = -1;
    int xp = -1;

    int selectedSlot = -1;

    uint64_t inventoryRevision = 0;
    uint64_t effectRevision = 0;

    bool crosshairVisible = true;

    bool valid = false;
};

static HudState gPreviousState;

// ============================================================
// Cache helpers
// ============================================================

static bool stateChanged(const HudState &state) {

    std::lock_guard<std::mutex> lock(gStateMutex);

    if (!gPreviousState.valid) {
        gPreviousState = state;
        gPreviousState.valid = true;
        return true;
    }

    const bool changed =
        gPreviousState.health != state.health ||
        gPreviousState.armor != state.armor ||
        gPreviousState.hunger != state.hunger ||
        gPreviousState.xp != state.xp ||
        gPreviousState.selectedSlot != state.selectedSlot ||
        gPreviousState.inventoryRevision != state.inventoryRevision ||
        gPreviousState.effectRevision != state.effectRevision ||
        gPreviousState.crosshairVisible != state.crosshairVisible;

    if (changed) {
        gPreviousState = state;
        gPreviousState.valid = true;
    }

    return changed;
}

static void invalidateCaches() {

    std::lock_guard<std::mutex> lock(gStateMutex);

    gPreviousState = {};
    gPreviousState.valid = false;
}

// ============================================================
// Optimizer policies
// ============================================================

bool shouldUpdateHUD(const HudState &state) {

    if (!gEnabled.load()) {
        return true;
    }

    if (!stateChanged(state)) {
        gStats.hudSkips++;
        return false;
    }

    gStats.hudUpdates++;
    return true;
}

bool shouldUpdateHotbar(uint64_t inventoryRevision) {

    if (!gEnabled.load()) {
        return true;
    }

    static uint64_t previous = UINT64_MAX;

    if (previous == inventoryRevision) {
        gStats.hotbarSkips++;
        return false;
    }

    previous = inventoryRevision;

    gStats.hotbarUpdates++;
    return true;
}

bool shouldUpdateItem(uint64_t itemRevision) {

    if (!gEnabled.load()) {
        return true;
    }

    static uint64_t previous = UINT64_MAX;

    if (previous == itemRevision) {
        gStats.itemSkips++;
        return false;
    }

    previous = itemRevision;

    gStats.itemUpdates++;
    return true;
}

bool shouldUpdateHealth(int health) {

    if (!gEnabled.load()) {
        return true;
    }

    static int previous = -1;

    if (previous == health) {
        gStats.healthSkips++;
        return false;
    }

    previous = health;

    gStats.healthUpdates++;
    return true;
}

bool shouldUpdateArmor(int armor) {

    if (!gEnabled.load()) {
        return true;
    }

    static int previous = -1;

    if (previous == armor) {
        gStats.armorSkips++;
        return false;
    }

    previous = armor;

    gStats.armorUpdates++;
    return true;
}

bool shouldUpdateHunger(int hunger) {

    if (!gEnabled.load()) {
        return true;
    }

    static int previous = -1;

    if (previous == hunger) {
        gStats.hungerSkips++;
        return false;
    }

    previous = hunger;

    gStats.hungerUpdates++;
    return true;
}

bool shouldUpdateXP(int xp) {

    if (!gEnabled.load()) {
        return true;
    }

    static int previous = -1;

    if (previous == xp) {
        gStats.xpSkips++;
        return false;
    }

    previous = xp;

    gStats.xpUpdates++;
    return true;
}

bool shouldUpdateCrosshair(bool visible) {

    if (!gEnabled.load()) {
        return true;
    }

    static bool previous = true;
    static bool valid = false;

    if (valid && previous == visible) {
        gStats.crosshairSkips++;
        return false;
    }

    previous = visible;
    valid = true;

    gStats.crosshairUpdates++;

    return true;
}

// ============================================================
// Statistics
// ============================================================

static uint64_t totalSkipped() {

    return
        gStats.hudSkips.load() +
        gStats.hotbarSkips.load() +
        gStats.itemSkips.load() +
        gStats.healthSkips.load() +
        gStats.armorSkips.load() +
        gStats.hungerSkips.load() +
        gStats.xpSkips.load() +
        gStats.crosshairSkips.load();
}

static void resetStatistics() {

    gStats.framesSeen = 0;

    gStats.hudUpdates = 0;
    gStats.hudSkips = 0;

    gStats.hotbarUpdates = 0;
    gStats.hotbarSkips = 0;

    gStats.itemUpdates = 0;
    gStats.itemSkips = 0;

    gStats.healthUpdates = 0;
    gStats.healthSkips = 0;

    gStats.hungerUpdates = 0;
    gStats.hungerSkips = 0;

    gStats.xpUpdates = 0;
    gStats.xpSkips = 0;

    gStats.crosshairUpdates = 0;
    gStats.crosshairSkips = 0;

    invalidateCaches();
}

// ============================================================
// Module callbacks
// ============================================================

static void onOptimizerToggle(
    std::string_view,
    bool enabled
) {
    gEnabled.store(enabled);
    invalidateCaches();
}

static void onAggressiveToggle(
    std::string_view,
    bool enabled
) {
    gAggressive.store(enabled);
    invalidateCaches();
}

} // namespace HUDOptimizer


// ============================================================
// HUD Optimizer Mod Main Class
// ============================================================

class HUDOptimizerMod {

public:

    static HUDOptimizerMod &instance() {
        static HUDOptimizerMod instance;
        return instance;
    }

    HUDOptimizerMod()
        : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]]
    ll::mod::NativeMod &getSelf() const {
        return mSelf;
    }

    bool load() {

        auto &logger = getSelf().getLogger();

        logger.info("HUD Optimizer 0.1.0 loading...");
        logger.info("Target: hotbar + HUD overlay");
        logger.info("Inventory screen is NOT modified");
        logger.info("FPS cap: OFF");
        logger.info("Frame generation: OFF");
        logger.info("Gameplay/input changes: NONE");

        return true;
    }

    bool enable() {

        auto &logger = getSelf().getLogger();
        auto &self = getSelf();

        try {
            // ============================================================
            // FIX: Use stable string references instead of temporaries
            // ============================================================
            
            bool registered = 
                pl::modmenu::ModuleBuilder(
                    HUDOptimizer::MODULE_ID_STR,        // Use static string
                    HUDOptimizer::MODULE_NAME           // Use static string
                )
                .modId(self.getId())
                .description(HUDOptimizer::MODULE_DESC)  // Use static string
                .defaultEnabled(true)
                .onToggle(HUDOptimizer::onOptimizerToggle)
                .registerModule();

            if (!registered) {
                logger.error(
                    "Failed to register HUD Optimizer module."
                );
                return false;
            }

            logger.info("HUD Optimizer enabled successfully.");
            logger.info("Waiting for verified Bedrock HUD hook.");

            return true;

        } catch (const std::exception &e) {
            logger.error(
                std::string("Exception during module registration: ") + e.what()
            );
            return false;
        } catch (...) {
            logger.error("Unknown exception during module registration");
            return false;
        }
    }

    bool disable() {

        auto &logger = getSelf().getLogger();

        try {
            HUDOptimizer::gEnabled.store(false);
            HUDOptimizer::invalidateCaches();

            pl::modmenu::unregisterModule(
                HUDOptimizer::MODULE_ID_STR
            );

            logger.info("HUD Optimizer disabled.");
            return true;

        } catch (const std::exception &e) {
            logger.error(
                std::string("Exception during disable: ") + e.what()
            );
            return false;
        }
    }

    bool unload() {

        auto &logger = getSelf().getLogger();

        try {
            HUDOptimizer::gEnabled.store(false);
            HUDOptimizer::invalidateCaches();

            logger.info("HUD Optimizer unloaded.");
            return true;

        } catch (const std::exception &e) {
            logger.error(
                std::string("Exception during unload: ") + e.what()
            );
            return false;
        }
    }

private:

    ll::mod::NativeMod &mSelf;
};


// ============================================================
// Mod registration
// ============================================================

PL_REGISTER_MOD(
    HUDOptimizerMod,
    HUDOptimizerMod::instance()
);
