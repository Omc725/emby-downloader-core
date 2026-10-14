#pragma once

#include "emby/models.hpp"
#include <string>

namespace Emby {

class AudioExtractor {
public:
    static DownloadResult extractAudioStream(
        const std::string& directAudioUrl,
        const std::string& fallbackVideoUrl,
        const StreamInfo& audioStream,
        const std::string& outputPath,
        double totalDurationSeconds = 0.0,
        ProgressCallback progressCb = nullptr,
        CancellationToken cancellationToken = nullptr
    );

private:
    static bool runFfmpegCommand(
        const std::string& inputUrl,
        const std::string& mapArg,
        const std::string& outputPath,
        double totalDurationSeconds,
        int bitRate,
        std::string& errorOutput,
        ProgressCallback progressCb,
        CancellationToken cancellationToken
    );
};

} // namespace Emby
