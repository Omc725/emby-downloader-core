#include "emby/config.hpp"
#include "emby/platform.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Emby {

ConfigManager::ConfigManager(const std::string& customDir) {
    if (!customDir.empty()) {
        m_configDir = customDir;
    } else {
        m_configDir = Platform::getDefaultConfigDir();
    }
    load();
}

std::string ConfigManager::getConfigFilePath() const {
    return m_configDir + "/config.json";
}

bool ConfigManager::load() {
    m_profiles.clear();
    m_lastActiveProfileId.clear();

    std::string path = getConfigFilePath();
    if (!fs::exists(path)) {
        // Geriye dönük uyumluluk: eski dizinleri kontrol et
        const char* home = std::getenv("HOME");
        if (home) {
            std::string legacyPath1 = std::string(home) + "/.config/emby-downloader/config.json";
            std::string legacyPath2 = std::string(home) + "/.config/emby-audio-downloader/config.json";
            if (fs::exists(legacyPath1)) {
                path = legacyPath1;
            } else if (fs::exists(legacyPath2)) {
                path = legacyPath2;
            }
        }
    }

    if (!fs::exists(path)) {
        return false;
    }

    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j;
        file >> j;

        if (j.contains("lastActiveProfile") && j["lastActiveProfile"].is_string()) {
            m_lastActiveProfileId = j["lastActiveProfile"].get<std::string>();
        }

        if (j.contains("profiles") && j["profiles"].is_array()) {
            for (const auto& item : j["profiles"]) {
                ServerProfile p;
                p.id = item.value("id", "");
                p.name = item.value("name", "");
                p.serverUrl = item.value("serverUrl", "");
                p.port = item.value("port", "443");
                p.prefix = item.value("prefix", "/emby");
                p.username = item.value("username", "");
                p.userId = item.value("userId", "");
                p.token = item.value("token", "");
                m_profiles.push_back(p);
            }
        } else if (j.contains("savedServers") && j["savedServers"].is_array()) {
            for (const auto& item : j["savedServers"]) {
                ServerProfile p;
                p.id = item.value("id", std::to_string(m_profiles.size() + 1));
                p.name = item.value("serverName", item.value("name", ""));
                p.serverUrl = item.value("serverUrl", "");
                p.port = item.value("port", "443");
                p.prefix = item.value("prefix", "/emby");
                p.username = item.value("username", "");
                p.userId = item.value("userId", "");
                p.token = item.value("token", "");
                if (p.name.empty()) {
                    p.name = p.username + " @ " + p.serverUrl;
                }
                m_profiles.push_back(p);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigManager::save() {
    try {
        fs::create_directories(m_configDir);
        std::ofstream file(getConfigFilePath());
        if (!file.is_open()) return false;

        json j;
        j["lastActiveProfile"] = m_lastActiveProfileId;
        j["profiles"] = json::array();

        for (const auto& p : m_profiles) {
            json item;
            item["id"] = p.id;
            item["name"] = p.name;
            item["serverUrl"] = p.serverUrl;
            item["port"] = p.port;
            item["prefix"] = p.prefix;
            item["username"] = p.username;
            item["userId"] = p.userId;
            item["token"] = p.token;
            j["profiles"].push_back(item);
        }

        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ServerProfile>& ConfigManager::getProfiles() {
    return m_profiles;
}

const std::vector<ServerProfile>& ConfigManager::getProfiles() const {
    return m_profiles;
}

std::optional<ServerProfile> ConfigManager::getProfile(const std::string& id) const {
    for (const auto& p : m_profiles) {
        if (p.id == id) return p;
    }
    return std::nullopt;
}

void ConfigManager::addOrUpdateProfile(const ServerProfile& profile) {
    for (auto& p : m_profiles) {
        if (p.id == profile.id || (p.serverUrl == profile.serverUrl && p.username == profile.username)) {
            p = profile;
            save();
            return;
        }
    }
    m_profiles.push_back(profile);
    save();
}

bool ConfigManager::removeProfile(const std::string& id) {
    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        if (it->id == id) {
            m_profiles.erase(it);
            if (m_lastActiveProfileId == id) {
                m_lastActiveProfileId = m_profiles.empty() ? "" : m_profiles[0].id;
            }
            save();
            return true;
        }
    }
    return false;
}

std::string ConfigManager::getLastActiveProfileId() const {
    return m_lastActiveProfileId;
}

void ConfigManager::setLastActiveProfileId(const std::string& id) {
    m_lastActiveProfileId = id;
    save();
}

} // namespace Emby
