#include "emby/downloader.hpp"
#include "emby/audio_extractor.hpp"
#include "emby/platform.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Emby {

struct CurlDownloadContext {
    FILE* file = nullptr;
    uint64_t resumeOffset = 0;
    uint64_t expectedTotal = 0;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastCallbackTime;
    ProgressCallback progressCallback;
    CancellationToken cancellationToken;
    PauseToken pauseToken;
    bool aborted = false;
    bool checkedRange = false;
    CURL* curlHandle = nullptr;
};

static size_t writeFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    CurlDownloadContext* ctx = static_cast<CurlDownloadContext*>(userp);
    if (!ctx || !ctx->file) return 0;

    if (!ctx->checkedRange && ctx->curlHandle) {
        ctx->checkedRange = true;
        long httpCode = 0;
        curl_easy_getinfo(ctx->curlHandle, CURLINFO_RESPONSE_CODE, &httpCode);
        if (httpCode == 200 && ctx->resumeOffset > 0) {
            std::fseek(ctx->file, 0, SEEK_SET);
#if defined(_WIN32)
            _chsize_s(_fileno(ctx->file), 0);
#else
            if (ftruncate(fileno(ctx->file), 0) != 0) {}
#endif
            ctx->resumeOffset = 0;
        }
    }

    return std::fwrite(contents, size, nmemb, ctx->file);
}

static int xferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    CurlDownloadContext* ctx = static_cast<CurlDownloadContext*>(clientp);
    if (!ctx) return 0;

    if (ctx->cancellationToken && ctx->cancellationToken->load()) {
        ctx->aborted = true;
        return 1; // Aborts curl
    }

    if (ctx->pauseToken && ctx->pauseToken->load()) {
        uint64_t curBytes = ctx->resumeOffset + static_cast<uint64_t>(dlnow);
        uint64_t totBytes = (dltotal > 0) ? (ctx->resumeOffset + static_cast<uint64_t>(dltotal)) : ctx->expectedTotal;

        if (ctx->progressCallback) {
            ProgressInfo pausedInfo;
            pausedInfo.currentBytes = curBytes;
            pausedInfo.totalBytes = totBytes;
            pausedInfo.speedBps = 0.0;
            if (totBytes > 0) {
                pausedInfo.percent = std::min(100.0, (static_cast<double>(curBytes) / static_cast<double>(totBytes)) * 100.0);
            }
            pausedInfo.remainingSeconds = 0;
            pausedInfo.statusMessage = "Paused";
            ctx->progressCallback(pausedInfo);
        }

        while (ctx->pauseToken && ctx->pauseToken->load()) {
            if (ctx->cancellationToken && ctx->cancellationToken->load()) {
                ctx->aborted = true;
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        ctx->startTime = std::chrono::steady_clock::now();
        ctx->lastCallbackTime = ctx->startTime;
    }

    if (!ctx->progressCallback) return 0;

    auto now = std::chrono::steady_clock::now();
    double msSinceLast = std::chrono::duration<double, std::milli>(now - ctx->lastCallbackTime).count();
    if (msSinceLast < 100.0 && dlnow < dltotal) {
        return 0;
    }
    ctx->lastCallbackTime = now;

    uint64_t currentBytes = ctx->resumeOffset + static_cast<uint64_t>(dlnow);
    uint64_t totalBytes = (dltotal > 0) ? (ctx->resumeOffset + static_cast<uint64_t>(dltotal)) : ctx->expectedTotal;

    double elapsedSec = std::chrono::duration<double>(now - ctx->startTime).count();
    double speedBps = (elapsedSec > 0.05) ? (static_cast<double>(dlnow) / elapsedSec) : 0.0;

    ProgressInfo info;
    info.currentBytes = currentBytes;
    info.totalBytes = totalBytes;
    info.speedBps = speedBps;
    if (totalBytes > 0) {
        info.percent = std::min(100.0, (static_cast<double>(currentBytes) / static_cast<double>(totalBytes)) * 100.0);
    }
    if (speedBps > 1024.0 && totalBytes > currentBytes) {
        info.remainingSeconds = static_cast<uint64_t>((totalBytes - currentBytes) / speedBps);
    }
    info.statusMessage = "Downloading";

    bool keepGoing = ctx->progressCallback(info);
    if (!keepGoing) {
        ctx->aborted = true;
        return 1;
    }

    return 0;
}

Downloader::Downloader(EmbyClient& client) : m_client(client) {}

DownloadResult Downloader::downloadFile(
    const std::string& url,
    const std::string& outputPath,
    uint64_t expectedSize,
    ProgressCallback progressCb,
    CancellationToken cancellationToken,
    PauseToken pauseToken,
    int maxRetries
) {
    DownloadResult result;
    result.path = outputPath;

    fs::path outP(outputPath);
    if (outP.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(outP.parent_path(), ec);
    }

    std::string partPath = outputPath + ".part";

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        if (cancellationToken && cancellationToken->load()) {
            result.error = "Operation cancelled";
            return result;
        }

        uint64_t existingBytes = 0;
        std::error_code ec;
        if (fs::exists(partPath, ec)) {
            existingBytes = fs::file_size(partPath, ec);
        }

        if (expectedSize > 0 && existingBytes >= expectedSize) {
            fs::remove(outputPath, ec);
            fs::rename(partPath, outputPath, ec);
            result.success = true;
            result.size = existingBytes;
            result.verified = (existingBytes == expectedSize);
            return result;
        }

        FILE* fp = std::fopen(partPath.c_str(), (existingBytes > 0) ? "ab" : "wb");
        if (!fp) {
            result.error = "Could not open file for writing: " + partPath;
            return result;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::fclose(fp);
            result.error = "Failed to initialize CURL";
            return result;
        }

        struct curl_slist* chunk = nullptr;
        for (const auto& [k, v] : m_client.getHeaders()) {
            std::string h = k + ": " + v;
            chunk = curl_slist_append(chunk, h.c_str());
        }

        CurlDownloadContext ctx;
        ctx.file = fp;
        ctx.resumeOffset = existingBytes;
        ctx.expectedTotal = expectedSize;
        ctx.startTime = std::chrono::steady_clock::now();
        ctx.lastCallbackTime = ctx.startTime;
        ctx.progressCallback = progressCb;
        ctx.cancellationToken = cancellationToken;
        ctx.pauseToken = pauseToken;
        ctx.aborted = false;
        ctx.checkedRange = false;
        ctx.curlHandle = curl;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

        if (existingBytes > 0) {
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(existingBytes));
        }

        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        std::fclose(fp);
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);

        if (ctx.aborted || (cancellationToken && cancellationToken->load())) {
            result.error = "Operation cancelled";
            return result;
        }

        bool ok = (res == CURLE_OK) && (httpCode >= 200 && httpCode < 300);
        if (ok) {
            fs::remove(outputPath, ec);
            fs::rename(partPath, outputPath, ec);

            uint64_t finalSize = fs::file_size(outputPath, ec);
            result.success = true;
            result.size = finalSize;
            result.verified = (expectedSize > 0 && finalSize == expectedSize);

            if (progressCb) {
                ProgressInfo finishedInfo;
                finishedInfo.currentBytes = finalSize;
                finishedInfo.totalBytes = (expectedSize > 0) ? expectedSize : finalSize;
                finishedInfo.percent = 100.0;
                finishedInfo.speedBps = 0.0;
                finishedInfo.remainingSeconds = 0;
                finishedInfo.statusMessage = "Completed";
                progressCb(finishedInfo);
            }
            return result;
        }

        if (attempt < maxRetries) {
            // Wait 2 seconds before retry, checking cancellation token
            for (int w = 0; w < 20; ++w) {
                if (cancellationToken && cancellationToken->load()) {
                    result.error = "Operation cancelled";
                    return result;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            result.error = "Download failed after " + std::to_string(maxRetries) + 
                           " attempts: " + curl_easy_strerror(res) + " (HTTP " + std::to_string(httpCode) + ")";
        }
    }

    return result;
}

DownloadResult Downloader::downloadVideo(
    const std::string& itemId,
    const MediaSource& source,
    const std::string& outputPath,
    uint64_t expectedSize,
    ProgressCallback progressCb,
    CancellationToken cancellationToken,
    PauseToken pauseToken
) {
    // Orijinal video dosyasını dönüştürmesiz birebir kopya olarak indir
    std::string downloadUrl = m_client.buildDirectVideoUrl(itemId, source);
    uint64_t targetSize = (expectedSize > 0) ? expectedSize : source.size;
    return downloadFile(downloadUrl, outputPath, targetSize, progressCb, cancellationToken, pauseToken);
}

DownloadResult Downloader::downloadAudio(
    const std::string& itemId,
    const MediaSource& source,
    const StreamInfo& audioStream,
    const std::string& outputPath,
    uint64_t expectedSize,
    double durationSeconds,
    ProgressCallback progressCb,
    CancellationToken cancellationToken,
    PauseToken pauseToken
) {
    uint64_t targetSize = expectedSize;
    if (targetSize == 0 && audioStream.bitRate > 0 && durationSeconds > 0) {
        targetSize = static_cast<uint64_t>(std::round((audioStream.bitRate / 8.0) * durationSeconds));
    }
    if (targetSize == 0) {
        targetSize = m_client.getAudioContentLength(itemId, source, audioStream);
    }

    std::string directUrl = m_client.buildDirectAudioStreamUrl(itemId, source, audioStream);

    // İlk deneme: libcurl ile doğrudan ses akışını indir
    DownloadResult res = downloadFile(directUrl, outputPath, targetSize, progressCb, cancellationToken, pauseToken, 2);
    if (res.success || (cancellationToken && cancellationToken->load())) {
        return res;
    }

    // Yedek çözüm: doğrudan akış başarısız olduysa AudioExtractor (ffmpeg) kullan
    std::string fallbackVideoUrl = m_client.buildDirectVideoUrl(itemId, source);
    return AudioExtractor::extractAudioStream(
        directUrl,
        fallbackVideoUrl,
        audioStream,
        outputPath,
        durationSeconds,
        progressCb,
        cancellationToken
    );
}

DownloadResult Downloader::downloadSubtitle(
    const std::string& itemId,
    const std::string& sourceId,
    const StreamInfo& subtitleStream,
    const std::string& outputPath,
    ProgressCallback progressCb,
    CancellationToken cancellationToken
) {
    DownloadResult result;
    result.path = outputPath;

    if (cancellationToken && cancellationToken->load()) {
        result.error = "Operation cancelled";
        return result;
    }

    fs::path outP(outputPath);
    if (outP.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(outP.parent_path(), ec);
    }

    std::string codec = subtitleStream.codec.empty() ? "srt" : subtitleStream.codec;
    std::string ext = (codec == "subrip" || codec == "srt") ? "srt" : codec;

    std::string subUrl = m_client.getSubtitleUrl(itemId, sourceId, subtitleStream.index, ext);
    HttpResponse res = m_client.httpGet(subUrl, m_client.getHeaders());

    if (!res.isOk() && ext != "srt") {
        std::string fallbackUrl = m_client.getSubtitleUrl(itemId, sourceId, subtitleStream.index, "srt");
        res = m_client.httpGet(fallbackUrl, m_client.getHeaders());
    }

    if (!res.isOk()) {
        result.error = "Subtitle download failed (HTTP " + std::to_string(res.statusCode) + "): " + res.error;
        return result;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        result.error = "Could not open subtitle file for writing: " + outputPath;
        return result;
    }

    out << res.body;
    out.close();

    std::error_code ec;
    uint64_t size = fs::file_size(outputPath, ec);
    result.success = true;
    result.size = size;
    result.verified = true;

    if (progressCb) {
        ProgressInfo finishedInfo;
        finishedInfo.currentBytes = size;
        finishedInfo.totalBytes = size;
        finishedInfo.percent = 100.0;
        finishedInfo.statusMessage = "Completed";
        progressCb(finishedInfo);
    }

    return result;
}

} // namespace Emby
