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
        case TaskStatus::Completed: return "completed";
        case TaskStatus::Error: return "error";
        case TaskStatus::Cancelled: return "cancelled";
    }
    return "pending";
}

static TaskStatus taskStatusFromString(const std::string& str) {
    if (str == "pending") return TaskStatus::Pending;
    if (str == "downloading") return TaskStatus::Downloading;
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
    t.status = taskStatusFromString(j.value("status", "pending"));
    t.error = j.value("error", "");
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

void QueueManager::start() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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
        m_isPaused.store(false);
        m_cv.notify_all();
        return;
    }

    m_stopRequested.store(false);
    m_isPaused.store(false);
    m_isRunning.store(true);
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_workerThread = std::thread(&QueueManager::workerThreadFunc, this);
}

void QueueManager::pause() {
    m_isPaused.store(true);
}

void QueueManager::resume() {
    m_isPaused.store(false);
    m_cv.notify_one();
}

void QueueManager::stop() {
    m_stopRequested.store(true);
    m_isPaused.store(false);
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
    if (m_currentCancellationToken) {
        m_currentCancellationToken->store(true);
    }
}

void QueueManager::cancelTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& t : m_tasks) {
        if (t.id == taskId) {
            if (t.id == m_currentTaskId && m_currentCancellationToken) {
                m_currentCancellationToken->store(true);
            }
            t.status = TaskStatus::Cancelled;
            break;
        }
    }
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
            m_currentCancellationToken
        );
    } else if (task.type == TaskType::Audio) {
        MediaSource source;
        source.id = task.sourceId;
        source.container = task.container;

        StreamInfo audioStream;
        audioStream.index = task.streamIndex;
        audioStream.codec = task.streamCodec;
        audioStream.language = task.streamLanguage;

        std::string directAudioUrl = m_client.buildDirectAudioStreamUrl(task.itemId, source, audioStream);
        std::string fallbackVideoUrl = m_client.buildDirectVideoUrl(task.itemId, source);

        result = AudioExtractor::extractAudioStream(
            directAudioUrl,
            fallbackVideoUrl,
            audioStream,
            task.outputPath,
            task.durationSeconds,
            progressCb,
            m_currentCancellationToken
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
