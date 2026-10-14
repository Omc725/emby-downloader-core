#include "emby/engine.hpp"
#include <iomanip>
#include <sstream>

namespace Emby {

static std::string buildMediaBaseFilename(const MediaItem& item, const MediaSource& source) {
    std::string safeName = Platform::sanitizeFilename(item.name.empty() ? "media" : item.name);
    std::string filename;

    if (item.type == MediaType::Episode) {
        std::string safeSeries = Platform::sanitizeFilename(item.seriesName.empty() ? "Series" : item.seriesName);
        std::ostringstream ss;
        ss << safeSeries << " - S" 
           << std::setfill('0') << std::setw(2) << item.seasonNumber
           << "E" << std::setfill('0') << std::setw(2) << item.episodeNumber
           << " - " << safeName;
        filename = ss.str();
    } else {
        if (item.year > 0) {
            filename = safeName + " (" + std::to_string(item.year) + ")";
        } else {
            filename = safeName;
        }
    }

    if (!source.name.empty()) {
        std::string safeSrc = Platform::sanitizeFilename(source.name);
        if (filename.find(safeSrc) == std::string::npos) {
            filename += " - " + safeSrc;
        }
    }

    return filename;
}

EmbyEngine::EmbyEngine(const std::string& configFilePath)
    : m_config(configFilePath)
{
    m_config.load();

    ServerProfile prof;
    bool hasProfile = false;
    std::string lastId = m_config.getLastActiveProfileId();
    if (!lastId.empty()) {
        auto opt = m_config.getProfile(lastId);
        if (opt.has_value()) {
            prof = opt.value();
            hasProfile = true;
        }
    }
    if (!hasProfile) {
        auto profiles = m_config.getProfiles();
        if (!profiles.empty()) {
            prof = profiles.front();
            hasProfile = true;
        }
    }

    if (hasProfile) {
        m_client = std::make_unique<EmbyClient>(prof.serverUrl, prof.port, prof.username, prof.prefix);
        m_client->setCredentials(prof.userId, prof.token);
    } else {
        m_client = std::make_unique<EmbyClient>();
    }

    m_downloader = std::make_unique<Downloader>(*m_client);
    m_queue = std::make_unique<QueueManager>(*m_client);
}

EmbyEngine::~EmbyEngine() {
    if (m_queue) {
        m_queue->stop();
    }
}

ConfigManager& EmbyEngine::getConfig() {
    return m_config;
}

const ConfigManager& EmbyEngine::getConfig() const {
    return m_config;
}

bool EmbyEngine::loadProfile(const std::string& profileId) {
    auto opt = m_config.getProfile(profileId);
    if (!opt.has_value()) {
        return false;
    }
    ServerProfile prof = opt.value();

    m_config.setLastActiveProfileId(profileId);
    m_config.save();

    m_client = std::make_unique<EmbyClient>(prof.serverUrl, prof.port, prof.username, prof.prefix);
    m_client->setCredentials(prof.userId, prof.token);
    m_downloader = std::make_unique<Downloader>(*m_client);
    m_queue = std::make_unique<QueueManager>(*m_client);
    return true;
}

bool EmbyEngine::connect(
    const std::string& serverUrl,
    const std::string& port,
    const std::string& username,
    const std::string& password,
    std::string& errorMsg,
    const std::string& prefix
) {
    auto client = std::make_unique<EmbyClient>(serverUrl, port, username, prefix);
    if (!client->authenticate(password, errorMsg)) {
        return false;
    }

    ServerProfile prof;
    prof.id = serverUrl + "_" + username;
    prof.name = username + "@" + serverUrl;
    prof.serverUrl = client->getServerUrl();
    prof.port = client->getPort();
    prof.prefix = client->getPrefix();
    prof.username = client->getUsername();
    prof.userId = client->getUserId();
    prof.token = client->getToken();

    m_config.addOrUpdateProfile(prof);
    m_config.setLastActiveProfileId(prof.id);
    m_config.save();

    m_client = std::move(client);
    m_downloader = std::make_unique<Downloader>(*m_client);
    m_queue = std::make_unique<QueueManager>(*m_client);
    return true;
}

bool EmbyEngine::testConnection() {
    return m_client ? m_client->testConnection() : false;
}

bool EmbyEngine::isAuthenticated() const {
    return m_client ? (!m_client->getToken().empty() && !m_client->getUserId().empty()) : false;
}

EmbyClient& EmbyEngine::getClient() {
    return *m_client;
}

std::vector<MediaItem> EmbyEngine::search(const std::string& query) {
    if (!m_client) return {};
    std::string q = Platform::trim(query);
    if (q.rfind("tt", 0) == 0 || q.rfind("tmdb:", 0) == 0 || q.rfind("imdb:", 0) == 0) {
        return m_client->searchById(q);
    }
    return m_client->searchByName(q);
}

MediaItem EmbyEngine::getItem(const std::string& itemId) {
    return m_client ? m_client->getItem(itemId) : MediaItem();
}

std::vector<MediaItem> EmbyEngine::getSeasons(const std::string& seriesId) {
    return m_client ? m_client->getSeasons(seriesId) : std::vector<MediaItem>();
}

std::vector<MediaItem> EmbyEngine::getEpisodes(const std::string& seriesId, const std::string& seasonId) {
    return m_client ? m_client->getEpisodes(seriesId, seasonId) : std::vector<MediaItem>();
}

Downloader& EmbyEngine::getDownloader() {
    return *m_downloader;
}

DownloadResult EmbyEngine::downloadVideo(
    const std::string& itemId,
    const MediaSource& source,
    const std::string& outputPath,
    uint64_t expectedSize,
    ProgressCallback progressCb,
    CancellationToken cancellationToken
) {
    return m_downloader->downloadVideo(itemId, source, outputPath, expectedSize, progressCb, cancellationToken);
}

DownloadResult EmbyEngine::downloadSubtitle(
    const std::string& itemId,
    const std::string& sourceId,
    const StreamInfo& subtitleStream,
    const std::string& outputPath,
    ProgressCallback progressCb,
    CancellationToken cancellationToken
) {
    return m_downloader->downloadSubtitle(itemId, sourceId, subtitleStream, outputPath, progressCb, cancellationToken);
}

DownloadResult EmbyEngine::extractAudio(
    const std::string& itemId,
    const MediaSource& source,
    const StreamInfo& audioStream,
    const std::string& outputPath,
    double totalDurationSeconds,
    ProgressCallback progressCb,
    CancellationToken cancellationToken
) {
    std::string directAudioUrl = m_client->buildDirectAudioStreamUrl(itemId, source, audioStream);
    std::string fallbackVideoUrl = m_client->buildDirectVideoUrl(itemId, source);
    return AudioExtractor::extractAudioStream(
        directAudioUrl,
        fallbackVideoUrl,
        audioStream,
        outputPath,
        totalDurationSeconds,
        progressCb,
        cancellationToken
    );
}

QueueManager& EmbyEngine::getQueue() {
    return *m_queue;
}

std::string EmbyEngine::enqueueVideo(
    const MediaItem& item,
    const MediaSource& source,
    const std::string& outputDir
) {
    std::string dir = outputDir.empty() ? getDefaultDownloadDir() : outputDir;
    std::string base = buildMediaBaseFilename(item, source);
    std::string ext = source.container.empty() ? "mkv" : source.container;
    std::string outPath = dir + "/" + base + "." + ext;

    QueueTask task;
    task.type = TaskType::Video;
    task.name = base;
    task.outputPath = outPath;
    task.itemId = item.id;
    task.sourceId = source.id;
    task.container = ext;
    task.expectedSize = source.size;
    task.durationSeconds = item.durationSeconds;

    return m_queue->addTask(task);
}

std::string EmbyEngine::enqueueAudio(
    const MediaItem& item,
    const MediaSource& source,
    const StreamInfo& audioStream,
    const std::string& outputDir
) {
    std::string dir = outputDir.empty() ? getDefaultDownloadDir() : outputDir;
    std::string base = buildMediaBaseFilename(item, source);
    std::string lang = audioStream.language.empty() ? "und" : audioStream.language;
    std::string ext = Platform::getAudioExtension(audioStream.codec);
    std::string outPath = dir + "/" + base + "." + lang + "." + ext;

    QueueTask task;
    task.type = TaskType::Audio;
    task.name = base + " (" + lang + " audio)";
    task.outputPath = outPath;
    task.itemId = item.id;
    task.sourceId = source.id;
    task.container = source.container.empty() ? "mkv" : source.container;
    task.durationSeconds = item.durationSeconds;
    task.streamIndex = audioStream.index;
    task.streamCodec = audioStream.codec;
    task.streamLanguage = lang;

    return m_queue->addTask(task);
}

std::string EmbyEngine::enqueueSubtitle(
    const MediaItem& item,
    const MediaSource& source,
    const StreamInfo& subtitleStream,
    const std::string& outputDir
) {
    std::string dir = outputDir.empty() ? getDefaultDownloadDir() : outputDir;
    std::string base = buildMediaBaseFilename(item, source);
    std::string lang = subtitleStream.language.empty() ? "und" : subtitleStream.language;
    std::string codec = subtitleStream.codec.empty() ? "srt" : subtitleStream.codec;
    std::string ext = (codec == "subrip" || codec == "srt") ? "srt" : codec;
    std::string outPath = dir + "/" + base + "." + lang + "." + ext;

    QueueTask task;
    task.type = TaskType::Subtitle;
    task.name = base + " (" + lang + " sub)";
    task.outputPath = outPath;
    task.itemId = item.id;
    task.sourceId = source.id;
    task.container = ext;
    task.durationSeconds = item.durationSeconds;
    task.streamIndex = subtitleStream.index;
    task.streamCodec = codec;
    task.streamLanguage = lang;

    return m_queue->addTask(task);
}

std::string EmbyEngine::getDefaultDownloadDir() const {
    return Platform::getDefaultDownloadDir();
}

bool EmbyEngine::isFfmpegAvailable() const {
    return Platform::checkFfmpegAvailable();
}

} // namespace Emby
