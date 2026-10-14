#pragma once

#include "emby/models.hpp"
#include "emby/client.hpp"
#include "emby/config.hpp"
#include "emby/downloader.hpp"
#include "emby/audio_extractor.hpp"
#include "emby/queue.hpp"
#include "emby/platform.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Emby {

class EmbyEngine {
public:
    explicit EmbyEngine(const std::string& configFilePath = "");
    ~EmbyEngine();

    // Profil ve Kimlik Yönetimi
    ConfigManager& getConfig();
    const ConfigManager& getConfig() const;
    bool loadProfile(const std::string& profileId);
    bool connect(const std::string& serverUrl, const std::string& port, const std::string& username, const std::string& password, std::string& errorMsg, const std::string& prefix = "/emby");
    bool testConnection();
    bool isAuthenticated() const;

    // Emby API İstemcisi
    EmbyClient& getClient();
    std::vector<MediaItem> search(const std::string& query);
    MediaItem getItem(const std::string& itemId);
    std::vector<MediaItem> getSeasons(const std::string& seriesId);
    std::vector<MediaItem> getEpisodes(const std::string& seriesId, const std::string& seasonId = "");

    // Doğrudan İndirme ve Dönüştürme
    Downloader& getDownloader();
    DownloadResult downloadVideo(
        const std::string& itemId,
        const MediaSource& source,
        const std::string& outputPath,
        uint64_t expectedSize = 0,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr
    );

    DownloadResult downloadSubtitle(
        const std::string& itemId,
        const std::string& sourceId,
        const StreamInfo& subtitleStream,
        const std::string& outputPath,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr
    );

    DownloadResult extractAudio(
        const std::string& itemId,
        const MediaSource& source,
        const StreamInfo& audioStream,
        const std::string& outputPath,
        double totalDurationSeconds = 0.0,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr
    );

    // Kuyruk Yöneticisi
    QueueManager& getQueue();
    std::string enqueueVideo(
        const MediaItem& item,
        const MediaSource& source,
        const std::string& outputDir = ""
    );
    std::string enqueueAudio(
        const MediaItem& item,
        const MediaSource& source,
        const StreamInfo& audioStream,
        const std::string& outputDir = ""
    );
    std::string enqueueSubtitle(
        const MediaItem& item,
        const MediaSource& source,
        const StreamInfo& subtitleStream,
        const std::string& outputDir = ""
    );

    // Platform yardımcıları
    std::string getDefaultDownloadDir() const;
    bool isFfmpegAvailable() const;

private:
    ConfigManager m_config;
    std::unique_ptr<EmbyClient> m_client;
    std::unique_ptr<Downloader> m_downloader;
    std::unique_ptr<QueueManager> m_queue;
};

} // namespace Emby
