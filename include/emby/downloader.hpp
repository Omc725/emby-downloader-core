#pragma once

#include "emby/models.hpp"
#include "emby/client.hpp"

#include <string>
#include <cstdint>

namespace Emby {

class Downloader {
public:
    explicit Downloader(EmbyClient& client);

    DownloadResult downloadVideo(
        const std::string& itemId,
        const MediaSource& source,
        const std::string& outputPath,
        uint64_t expectedSize = 0,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr,
        PauseToken pauseToken = nullptr
    );

    DownloadResult downloadAudio(
        const std::string& itemId,
        const MediaSource& source,
        const StreamInfo& audioStream,
        const std::string& outputPath,
        uint64_t expectedSize = 0,
        double durationSeconds = 0.0,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr,
        PauseToken pauseToken = nullptr
    );

    DownloadResult downloadSubtitle(
        const std::string& itemId,
        const std::string& sourceId,
        const StreamInfo& subtitleStream,
        const std::string& outputPath,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr
    );

    DownloadResult downloadFile(
        const std::string& url,
        const std::string& outputPath,
        uint64_t expectedSize = 0,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr,
        PauseToken pauseToken = nullptr,
        int maxRetries = 5
    );

private:
    EmbyClient& m_client;
};

} // namespace Emby
