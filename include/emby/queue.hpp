#pragma once

#include "emby/models.hpp"
#include "emby/client.hpp"
#include "emby/downloader.hpp"
#include "emby/audio_extractor.hpp"

#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>

namespace Emby {

class QueueManager {
public:
    using TaskCallback = std::function<void(const QueueTask&)>;
    using ProgressEventCallback = std::function<void(const QueueTask&, const ProgressInfo&)>;
    using ResultCallback = std::function<void(const QueueTask&, const DownloadResult&)>;
    using ErrorCallback = std::function<void(const QueueTask&, const std::string&)>;
    using QueueFinishedCallback = std::function<void()>;

    explicit QueueManager(EmbyClient& client);
    ~QueueManager();

    std::string addTask(const QueueTask& task);
    void addTasks(const std::vector<QueueTask>& tasks);
    bool removeTask(const std::string& taskId);
    void clear();

    std::vector<QueueTask> getTasks() const;
    QueueTask getTask(const std::string& taskId) const;
    size_t getPendingCount() const;
    bool isRunning() const;

    void start();
    void pause();
    void resume();
    void stop();
    void cancelCurrentTask();
    void cancelTask(const std::string& taskId);
    void retryTask(const std::string& taskId);
    void retryAllFailed();

    bool loadFromFile(const std::string& filePath = "");
    bool saveToFile(const std::string& filePath = "") const;

    void setOnTaskAdded(TaskCallback cb) { m_onTaskAdded = cb; }
    void setOnTaskStarted(TaskCallback cb) { m_onTaskStarted = cb; }
    void setOnTaskProgress(ProgressEventCallback cb) { m_onTaskProgress = cb; }
    void setOnTaskCompleted(ResultCallback cb) { m_onTaskCompleted = cb; }
    void setOnTaskError(ErrorCallback cb) { m_onTaskError = cb; }
    void setOnQueueFinished(QueueFinishedCallback cb) { m_onQueueFinished = cb; }

    static std::string getDefaultQueueFilePath();

private:
    void workerThreadFunc();
    void executeTask(QueueTask& task);

    EmbyClient& m_client;
    Downloader m_downloader;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<QueueTask> m_tasks;

    std::thread m_workerThread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isRunning{false};

    CancellationToken m_currentCancellationToken;
    std::string m_currentTaskId;

    TaskCallback m_onTaskAdded;
    TaskCallback m_onTaskStarted;
    ProgressEventCallback m_onTaskProgress;
    ResultCallback m_onTaskCompleted;
    ErrorCallback m_onTaskError;
    QueueFinishedCallback m_onQueueFinished;
};

} // namespace Emby
