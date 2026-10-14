#include "emby/engine.hpp"
#include "emby/c_api.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    std::cout << "[EMBY-CORE] Emby Core Engine Test Harness\n";
    std::cout << "[EMBY-CORE] Platform Default Download Dir: " << Emby::Platform::getDefaultDownloadDir() << "\n";
    std::cout << "[EMBY-CORE] Platform Default Config Dir:   " << Emby::Platform::getDefaultConfigDir() << "\n";
    std::cout << "[EMBY-CORE] FFmpeg Available:              " << (Emby::Platform::checkFfmpegAvailable() ? "Yes" : "No") << "\n";

    // 1. C++ Facade Initialization
    Emby::EmbyEngine engine;
    auto profiles = engine.getConfig().getProfiles();
    std::cout << "[EMBY-CORE] Configured Profiles Count:     " << profiles.size() << "\n";
    for (const auto& p : profiles) {
        std::cout << "  - Profile: " << p.name << " (" << p.serverUrl << ")\n";
    }

    if (engine.isAuthenticated()) {
        std::cout << "[EMBY-CORE] Active Session Authenticated. Testing connection...\n";
        bool ok = engine.testConnection();
        std::cout << "[EMBY-CORE] Connection Test: " << (ok ? "OK" : "FAILED") << "\n";

        if (ok && argc > 1) {
            std::string query = argv[1];
            std::cout << "[EMBY-CORE] Searching for: " << query << "\n";
            auto results = engine.search(query);
            std::cout << "[EMBY-CORE] Found " << results.size() << " items.\n";
            for (const auto& item : results) {
                std::cout << "  * [" << item.id << "] " << item.name << " (" << item.year << ") - "
                          << item.sources.size() << " source(s)\n";
            }
        }
    } else {
        std::cout << "[EMBY-CORE] No authenticated session active (Headless initialization OK).\n";
    }

    // 2. C ABI Interface Verification
    std::cout << "[EMBY-CORE] Verifying C ABI Bindings...\n";
    emby_engine_t* cEngine = emby_engine_create(nullptr);
    if (cEngine) {
        char* profJson = emby_engine_get_profiles_json(cEngine);
        std::cout << "[EMBY-CORE] C ABI Profiles JSON: " << (profJson ? profJson : "null") << "\n";
        emby_string_free(profJson);

        char* defDir = emby_get_default_download_dir();
        std::cout << "[EMBY-CORE] C ABI Default Dir:   " << (defDir ? defDir : "null") << "\n";
        emby_string_free(defDir);

        emby_engine_destroy(cEngine);
        std::cout << "[EMBY-CORE] C ABI Engine destroyed successfully.\n";
    }

    std::cout << "[EMBY-CORE] Engine test completed successfully.\n";
    return 0;
}
