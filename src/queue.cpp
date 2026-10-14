#include "emby/queue.hpp"
#include "emby/platform.hpp"

#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <chrono>

namespace fs = std::filesystem;

namespace Emby {

static std::string taskTypeToString(TaskType type) {
    switch (type) {
        case TaskType::Video: return "video";
        case TaskType::Audio: return "audio";
        case TaskType::Subtitle: return "subtitle";
    }
    return "unknown";
}

static TaskType taskTypeFromString(const std::string& str) {
    if (str == "video") return TaskType::Video;
    if (str == "audio") return TaskType::Audio;
    if (str == "subtitle") return TaskType::Subtitle;
    return TaskType::Video;
}

static std::string taskStatusToString(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending: return "pending";
        case TaskStatus::Downloading: return "downloading";
        case TaskStatus::Paused: return "paused";
        case TaskStatus::Completed: return "completed";
        case TaskStatus::Error: return "error";
        case TaskStatus::Cancelled: return "cancelled";
    }
    return "pending";
}

static TaskStatus taskStatusFromString(const std::string& str) {
    if (str == "pending") return TaskStatus::Pending;
    if (str == "downloading") return TaskStatus::Downloading;
    if (str == "paused") return TaskStatus::Paused;
    if (str == "completed") return TaskStatus::Completed;
    if (str == "error") return TaskStatus::Error;
    if (str == "cancelled") return TaskStatus::Cancelled;
    return TaskStatus::Pending;
}

static nlohmann::json taskToJson(const QueueTask& task) {
    nlohmann::json j;
    j["id"] = task.id;
    j["type"] = taskTypeToString(task.type);
    j["name"] = task.name;
    j["outputPath"] = task.outputPath;
    j["itemId"] = task.itemId;
    j["sourceId"] = task.sourceId;
    j["container"] = task.container;
    j["expectedSize"] = task.expectedSize;
    j["durationSeconds"] = task.durationSeconds;
    j["streamIndex"] = task.streamIndex;
    j["streamCodec"] = task.streamCodec;
    j["streamLanguage"] = task.streamLanguage;
    j["streamBitrate"] = task.streamBitrate;
    j["status"] = taskStatusToString(task.status);
    j["error"] = task.error;
    return j;
}

static QueueTask jsonToTask(const nlohmann::json& j) {
    QueueTask t;
    t.id = j.value("id", "");
    t.type = taskTypeFromString(j.value("type", "video"));
    t.name = j.value("name", "");
    t.outputPath = j.value("outputPath", "");
    t.itemId = j.value("itemId", "");
    t.sourceId = j.value("sourceId", "");
    t.container = j.value("container", "mkv");
    t.expectedSize = j.value("expectedSize", 0ULL);
    t.durationSeconds = j.value("durationSeconds", 0.0);
    t.streamIndex = j.value("streamIndex", 0);
    t.streamCodec = j.value("streamCodec", "");
    t.streamLanguage = j.value("streamLanguage", "");
    t.streamBitrate = j.value("streamBitrate", 0);
    t.status = taskStatusFromString(j.value("status", "pending"));
    t.error = j.value("error", "");

    std::string partPath = t.outputPath + ".part";
    std::error_code ec;
    if (fs::exists(partPath, ec)) {
        t.progress.currentBytes = fs::file_size(partPath, ec);
        t.progress.totalBytes = t.expectedSize;
        if (t.expectedSize > 0) {
            t.progress.percent = std::min(100.0, (static_cast<double>(t.progress.currentBytes) / static_cast<double>(t.expectedSize)) * 100.0);
        }
    }
    return t;
}

QueueManager::QueueManager(EmbyClient& client)
    : m_client(client),
      m_downloader(client)
{
}

QueueManager::~QueueManager() {
    stop();
}

std::string QueueManager::getDefaultQueueFilePath() {
    return Platform::getDefaultConfigDir() + "/queue.json";
}

std::string QueueManager::addTask(const QueueTask& task) {
    std::string newId;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        QueueTask newTask = task;
        if (newTask.id.empty()) {
            newTask.id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        }
        newTask.status = TaskStatus::Pending;
        m_tasks.push_back(newTask);
        newId = newTask.id;

        if (m_onTaskAdded) {
            m_onTaskAdded(newTask);
        }
        m_cv.notify_one();
    }
    saveToFile();
    return newId;
}

void QueueManager::addTasks(const std::vector<QueueTask>& tasks) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto task : tasks) {
            if (task.id.empty()) {
                task.id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "_" + std::to_string(rand() % 1000);
            }
            task.status = TaskStatus::Pending;
            m_tasks.push_back(task);
            if (m_onTaskAdded) {
                m_onTaskAdded(task);
            }
        }
        m_cv.notify_one();
    }
    saveToFile();
}

bool QueueManager::removeTask(const std::string& taskId) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            if (it->id == taskId) {
                if (it->id == m_currentTaskId && m_currentCancellationToken) {
                    m_currentCancellationToken->store(true);
                }
                m_tasks.erase(it);
                removed = true;
                break;
            }
        }
    }
    if (removed) {
        saveToFile();
    }
    return removed;
}

void QueueManager::clear() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentCancellationToken) {
            m_currentCancellationToken->store(true);
        }
        m_tasks.clear();
    }
    saveToFile();
}

std::vector<QueueTask> QueueManager::getTasks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks;
}

QueueTask QueueManager::getTask(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& t : m_tasks) {
        if (t.id == taskId) return t;
    }
    return QueueTask();
}

size_t QueueManager::getPendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& t : m_tasks) {
        if (t.status == TaskStatus::Pending) count++;
    }
    return count;
}

bool QueueManager::isRunning() const {
    return m_isRunning.load();
}

bool QueueManager::isPaused() const {
    return m_isPaused.load();
}

void QueueManager::start() {
    m_isPaused.store(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentPauseToken) {
            m_currentPauseToken->store(false);
        }
        for (auto& t : m_tasks) {
            if (t.status == TaskStatus::Paused) {
                if (t.id == m_currentTaskId) {
                    t.status = TaskStatus::Downloading;
                } else {
                    t.status = TaskStatus::Pending;
                }
            }
        }

        bool hasPending = false;
        for (const auto& t : m_tasks) {
            if (t.status == TaskStatus::Pending || t.status == TaskStatus::Downloading) {
                hasPending = true;
                break;
            }
        }
        if (!hasPending) {
            for (auto& t : m_tasks) {
                if (t.status == TaskStatus::Error || t.status == TaskStatus::Cancelled) {
                    t.status = TaskStatus::Pending;
                    t.error.clear();
                }
            }
        }
    }

    if (m_isRunning.load()) {
        m_cv.notify_all();
        return;
    }

    m_stopRequested.store(false);
    m_isRunning.store(true);
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_workerThread = std::thread(&QueueManager::workerThreadFunc, this);
}

void QueueManager::pause() {
    m_isPaused.store(true);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentPauseToken) {
        m_currentPauseToken->store(true);
    }
    for (auto& t : m_tasks) {
        if (t.id == m_currentTaskId) {
            t.status = TaskStatus::Paused;
            if (m_onTaskProgress) {
                m_onTaskProgress(t, t.progress);
            }
        }
    }
    saveToFile();
}

void QueueManager::resume() {
    start();
}

void QueueManager::pauseTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& t : m_tasks) {
        if (t.id == taskId) {
            t.status = TaskStatus::Paused;
            if (t.id == m_currentTaskId && m_currentPauseToken) {
                m_currentPauseToken->store(true);
            }
            if (m_onTaskProgress) {
                m_onTaskProgress(t, t.progress);
            }
            break;
        }
    }
    saveToFile();
}

void QueueManager::resumeTask(const std::string& taskId) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& t : m_tasks) {
            if (t.id == taskId) {
                if (t.id == m_currentTaskId && m_currentPauseToken) {
                    t.status = TaskStatus::Downloading;
                    m_currentPauseToken->store(false);
                } else {
                    t.status = TaskStatus::Pending;
                }
                break;
            }
        }
    }
    saveToFile();
    start();
}

void QueueManager::stop() {
    m_stopRequested.store(true);
    m_isPaused.store(false);
    if (m_currentPauseToken) {
        m_currentPauseToken->store(false);
    }
    if (m_currentCancellationToken) {
        m_currentCancellationToken->store(true);
    }
    m_cv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_isRunning.store(false);
}

void QueueManager::cancelCurrentTask() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentPauseToken) {
        m_currentPauseToken->store(false);
    }
    if (m_currentCancellationToken) {
        m_currentCancellationToken->store(true);
    }
}

void QueueManager::cancelTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& t : m_tasks) {
        if (t.id == taskId) {
            if (t.id == m_currentTaskId) {
                if (m_currentPauseToken) {
                    m_currentPauseToken->store(false);
                }
                if (m_currentCancellationToken) {
                    m_currentCancellationToken->store(true);
                }
            }
            t.status = TaskStatus::Cancelled;
            break;
        }
    }
    saveToFile();
}

void QueueManager::retryTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& t : m_tasks) {
        if (t.id == taskId) {
            t.status = TaskStatus::Pending;
            t.error.clear();
            break;
        }
    }
    m_cv.notify_one();
}

void QueueManager::retryAllFailed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& t : m_tasks) {
        if (t.status == TaskStatus::Error || t.status == TaskStatus::Cancelled) {
            t.status = TaskStatus::Pending;
            t.error.clear();
        }
    }
    m_cv.notify_one();
}

bool QueueManager::deleteTaskFiles(const std::string& taskId, bool deletePartOnly) {
    std::string outputPath;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& t : m_tasks) {
            if (t.id == taskId) {
                outputPath = t.outputPath;
                if (t.id == m_currentTaskId) {
                    if (m_currentPauseToken) m_currentPauseToken->store(false);
                    if (m_currentCancellationToken) m_currentCancellationToken->store(true);
                }
                break;
            }
        }
    }

    if (outputPath.empty()) return false;

    std::error_code ec;
    std::string partPath = outputPath + ".part";
    if (fs::exists(partPath, ec)) {
        fs::remove(partPath, ec);
    }
    if (!deletePartOnly && fs::exists(outputPath, ec)) {
        fs::remove(outputPath, ec);
    }
    return true;
}

std::string QueueManager::getTaskDownloadUrl(const std::string& taskId) const {
    QueueTask task = getTask(taskId);
    if (task.id.empty()) return "";

    if (task.type == TaskType::Video) {
        MediaSource source;
        source.id = task.sourceId;
        source.container = task.container;
        return m_client.buildDirectVideoUrl(task.itemId, source);
    } else if (task.type == TaskType::Audio) {
        MediaSource source;
        source.id = task.sourceId;
        source.container = task.container;
        StreamInfo audioStream;
        audioStream.index = task.streamIndex;
        audioStream.codec = task.streamCodec;
        return m_client.buildDirectAudioStreamUrl(task.itemId, source, audioStream);
    } else if (task.type == TaskType::Subtitle) {
        return m_client.getSubtitleUrl(task.itemId, task.sourceId, task.streamIndex, task.streamCodec);
    }
    return "";
}

bool QueueManager::loadFromFile(const std::string& filePath) {
    std::string path = filePath.empty() ? getDefaultQueueFilePath() : filePath;
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;
        if (j.is_array()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.clear();
            for (const auto& item : j) {
                m_tasks.push_back(jsonToTask(item));
            }
            return true;
        }
    } catch (...) {}
    return false;
}

bool QueueManager::saveToFile(const std::string& filePath) const {
    std::string path = filePath.empty() ? getDefaultQueueFilePath() : filePath;
    fs::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    nlohmann::json j = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& t : m_tasks) {
            j.push_back(taskToJson(t));
        }
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

void QueueManager::workerThreadFunc() {
    while (!m_stopRequested.load()) {
        QueueTask activeTask;
        bool hasTask = false;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]() {
                if (m_stopRequested.load()) return true;
                if (m_isPaused.load()) return false;
                for (const auto& t : m_tasks) {
                    if (t.status == TaskStatus::Pending) return true;
                }
                return false;
            });

            if (m_stopRequested.load()) break;

            for (auto& t : m_tasks) {
                if (t.status == TaskStatus::Pending) {
                    t.status = TaskStatus::Downloading;
                    activeTask = t;
                    hasTask = true;
                    m_currentTaskId = t.id;
                    m_currentCancellationToken = makeCancellationToken();
                    m_currentPauseToken = makePauseToken();
                    break;
                }
            }
        }

        if (hasTask) {
            if (m_onTaskStarted) {
                m_onTaskStarted(activeTask);
            }

            executeTask(activeTask);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_currentTaskId.clear();
                m_currentCancellationToken = nullptr;
                m_currentPauseToken = nullptr;
                for (auto& t : m_tasks) {
                    if (t.id == activeTask.id) {
                        t.status = activeTask.status;
                        t.progress = activeTask.progress;
                        t.error = activeTask.error;
                        break;
                    }
                }
            }
            saveToFile();
        } else {
            if (m_onQueueFinished) {
                m_onQueueFinished();
            }
        }
    }

    m_isRunning.store(false);
}

void QueueManager::executeTask(QueueTask& task) {
    auto progressCb = [&](const ProgressInfo& info) -> bool {
        if (m_stopRequested.load()) return false;
        task.progress = info;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& t : m_tasks) {
                if (t.id == task.id) {
                    t.progress = info;
                    break;
                }
            }
        }
        if (m_onTaskProgress) {
            m_onTaskProgress(task, info);
        }
        return true;
    };

    DownloadResult result;

    if (task.type == TaskType::Video) {
        MediaSource source;
        source.id = task.sourceId;
        source.container = task.container;
        source.size = task.expectedSize;

        result = m_downloader.downloadVideo(
            task.itemId,
            source,
            task.outputPath,
            task.expectedSize,
            progressCb,
            m_currentCancellationToken,
            m_currentPauseToken
        );
    } else if (task.type == TaskType::Audio) {
        MediaSource source;
        source.id = task.sourceId;
        source.container = task.container;

        StreamInfo audioStream;
        audioStream.index = task.streamIndex;
        audioStream.codec = task.streamCodec;
        audioStream.language = task.streamLanguage;
        audioStream.bitRate = task.streamBitrate;

        result = m_downloader.downloadAudio(
            task.itemId,
            source,
            audioStream,
            task.outputPath,
            task.expectedSize,
            task.durationSeconds,
            progressCb,
            m_currentCancellationToken,
            m_currentPauseToken
        );
    } else if (task.type == TaskType::Subtitle) {
        StreamInfo subStream;
        subStream.index = task.streamIndex;
        subStream.codec = task.streamCodec;
        subStream.language = task.streamLanguage;

        result = m_downloader.downloadSubtitle(
            task.itemId,
            task.sourceId,
            subStream,
            task.outputPath,
            progressCb,
            m_currentCancellationToken
        );
    }

    if (result.success) {
        task.status = TaskStatus::Completed;
        if (m_onTaskCompleted) {
            m_onTaskCompleted(task, result);
        }
    } else if (m_isPaused.load() || (m_currentPauseToken && m_currentPauseToken->load())) {
        task.status = TaskStatus::Paused;
        task.error.clear();
    } else if (result.error == "Operation cancelled" || (m_currentCancellationToken && m_currentCancellationToken->load())) {
        task.status = TaskStatus::Cancelled;
        task.error = "Cancelled";
        if (m_onTaskError) {
            m_onTaskError(task, "Cancelled");
        }
    } else {
        task.status = TaskStatus::Error;
        task.error = result.error;
        if (m_onTaskError) {
            m_onTaskError(task, result.error);
        }
    }
}

} // namespace Emby
