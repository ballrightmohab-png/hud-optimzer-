#include <android/log.h>
#include <atomic>
#include <cstdint>

#define LOG_TAG "HUDOptimizer"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace HUDOptimizer {

static std::atomic<bool> initialized{false};

static constexpr const char* VERSION = "0.1.0";

static void initialize() {
    bool expected = false;

    if (!initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    LOGI("=================================");
    LOGI("HUD Optimizer %s", VERSION);
    LOGI("Module loaded successfully");
    LOGI("Safe prototype mode");
    LOGI("No HUD elements removed");
    LOGI("No FPS cap");
    LOGI("No frame generation");
    LOGI("=================================");
}

static void shutdown() {
    bool expected = true;

    if (!initialized.compare_exchange_strong(expected, false)) {
        return;
    }

    LOGI("HUD Optimizer shutting down");
}

} // namespace HUDOptimizer

extern "C" __attribute__((constructor))
void hud_optimizer_load() {
    HUDOptimizer::initialize();
}

extern "C" __attribute__((destructor))
void hud_optimizer_unload() {
    HUDOptimizer::shutdown();
}
