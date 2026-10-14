#pragma once

#include "emby/models.hpp"

#include <string>
#include <vector>
#include <optional>

namespace Emby {

class ConfigManager {
public:
    explicit ConfigManager(const std::string& customDir = "");

    bool load();
    bool save();

    std::vector<ServerProfile>& getProfiles();
    const std::vector<ServerProfile>& getProfiles() const;
    std::optional<ServerProfile> getProfile(const std::string& id) const;
    void addOrUpdateProfile(const ServerProfile& profile);
    bool removeProfile(const std::string& id);

    std::string getLastActiveProfileId() const;
    void setLastActiveProfileId(const std::string& id);

    std::string getConfigDir() const { return m_configDir; }
    std::string getConfigFilePath() const;

private:
    std::string m_configDir;
    std::vector<ServerProfile> m_profiles;
    std::string m_lastActiveProfileId;
};

} // namespace Emby
