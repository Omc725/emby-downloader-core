#pragma once

#include "emby/models.hpp"

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace Emby {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;

    bool isOk() const { return statusCode >= 200 && statusCode < 300; }
};

class EmbyClient {
public:
    EmbyClient(
        const std::string& serverUrl = "",
        const std::string& port = "443",
        const std::string& username = "",
        const std::string& prefix = "/emby"
    );

    void setCredentials(const std::string& userId, const std::string& token);
    std::string getServerUrl() const { return m_serverUrl; }
    std::string getPort() const { return m_port; }
    std::string getPrefix() const { return m_prefix; }
    std::string getUsername() const { return m_username; }
    std::string getUserId() const { return m_userId; }
    std::string getToken() const { return m_token; }

    std::map<std::string, std::string> getHeaders() const;

    bool authenticate(const std::string& password, std::string& errorMsg);
    bool testConnection();

    std::vector<MediaItem> searchByName(const std::string& query);
    std::vector<MediaItem> searchById(const std::string& idInput);

    MediaItem getItem(const std::string& itemId);
    std::vector<MediaItem> getSeasons(const std::string& seriesId);
    std::vector<MediaItem> getEpisodes(const std::string& seriesId, const std::string& seasonId = "");

    std::string buildDirectVideoUrl(const std::string& itemId, const MediaSource& source) const;
    std::string buildDirectAudioStreamUrl(const std::string& itemId, const MediaSource& source, const StreamInfo& audioStream) const;
    uint64_t getAudioContentLength(const std::string& itemId, const MediaSource& source, const StreamInfo& audioStream) const;
    std::string getSubtitleUrl(const std::string& itemId, const std::string& sourceId, int subIndex, const std::string& ext) const;
    std::string getOfficialDownloadUrl(const std::string& itemId) const;
    bool checkOfficialDownloadAvailable(const std::string& itemId);

    // Düşük seviyeli HTTP istekleri
    HttpResponse httpGet(const std::string& url, const std::map<std::string, std::string>& headers = {});
    HttpResponse httpPostJson(const std::string& url, const std::string& jsonBody, const std::map<std::string, std::string>& headers = {});
    HttpResponse httpHead(const std::string& url, const std::map<std::string, std::string>& headers = {});

    static MediaItem parseJsonToMediaItem(const nlohmann::json& j);

private:
    std::string m_serverUrl;
    std::string m_port;
    std::string m_prefix;
    std::string m_username;
    std::string m_userId;
    std::string m_token;

    static std::string normalizeUrl(const std::string& url, const std::string& port);
    nlohmann::json fetchTmdbMetadata(const std::string& cleanId, bool isImdb);
};

} // namespace Emby
