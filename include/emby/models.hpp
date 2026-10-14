#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <atomic>

namespace Emby {

enum class MediaType {
    Movie,
    Series,
    Season,
    Episode,
    Unknown
};

enum class TaskType {
    Video,
    Audio,
    Subtitle
};

enum class TaskStatus {
    Pending,
    Downloading,
    Completed,
    Error,
    Cancelled
};

struct ProgressInfo {
    uint64_t currentBytes = 0;
    uint64_t totalBytes = 0;
    double speedBps = 0.0;
    double percent = 0.0;
    uint64_t remainingSeconds = 0;
    std::string statusMessage;
};

using ProgressCallback = std::function<bool(const ProgressInfo&)>;
using CancellationToken = std::shared_ptr<std::atomic<bool>>;

inline CancellationToken makeCancellationToken() {
    return std::make_shared<std::atomic<bool>>(false);
}

struct StreamInfo {
    int index = 0;
    std::string type;       // "Audio" veya "Subtitle"
    std::string codec;
    std::string language;
    std::string title;
    std::string channels;
    int bitRate = 0;
    bool isDefault = false;
    bool isForced = false;
};

struct MediaSource {
    std::string id;
    std::string name;
    std::string container = "mkv";
    std::string path;
    uint64_t size = 0;
    int width = 0;
    int height = 0;
    std::vector<StreamInfo> audioStreams;
    std::vector<StreamInfo> subtitleStreams;
};

struct MediaItem {
    std::string id;
    std::string name;
    std::string originalTitle;
    int year = 0;
    MediaType type = MediaType::Unknown;
    std::string seriesId;
    std::string seriesName;
    int seasonNumber = 0;
    int episodeNumber = 0;
    double durationSeconds = 0.0;
    std::vector<MediaSource> sources;
};

struct ServerProfile {
    std::string id;
    std::string name;
    std::string serverUrl;
    std::string port = "443";
    std::string prefix = "/emby";
    std::string username;
    std::string userId;
    std::string token;
};

struct DownloadResult {
    bool success = false;
    std::string path;
    uint64_t size = 0;
    bool verified = false;
    std::string error;
};

struct QueueTask {
    std::string id;
    TaskType type = TaskType::Video;
    std::string name;
    std::string outputPath;
    std::string itemId;
    std::string sourceId;
    std::string container = "mkv";
    uint64_t expectedSize = 0;
    double durationSeconds = 0.0;
    int streamIndex = 0;
    std::string streamCodec;
    std::string streamLanguage;
    TaskStatus status = TaskStatus::Pending;
    ProgressInfo progress;
    std::string error;
};

} // namespace Emby
