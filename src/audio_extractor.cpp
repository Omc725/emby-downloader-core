#include "emby/audio_extractor.hpp"
#include "emby/platform.hpp"

#include <filesystem>
#include <sstream>
#include <regex>
#include <chrono>
#include <cmath>

namespace fs = std::filesystem;

namespace Emby {

bool AudioExtractor::runFfmpegCommand(
    const std::string& inputUrl,
    const std::string& mapArg,
    const std::string& outputPath,
    double totalDurationSeconds,
    int bitRate,
    std::string& errorOutput,
    ProgressCallback progressCb,
    CancellationToken cancellationToken
) {
    std::ostringstream cmd;
    cmd << "ffmpeg -y -hide_banner -loglevel info -stats "
        << "-reconnect 1 -reconnect_at_eof 1 -reconnect_streamed 1 "
        << "-reconnect_on_network_error 1 -reconnect_delay_max 15 "
        << "-i \"" << inputUrl << "\" ";

    if (!mapArg.empty()) {
        cmd << "-map " << mapArg << " ";
    }

    cmd << "-vn -c:a copy \"" << outputPath << "\" 2>&1";

    FILE* pipe = Platform::executeProcess(cmd.str(), "r");
    if (!pipe) {
        errorOutput = "Failed to launch ffmpeg process";
        return false;
    }

    uint64_t expectedTotalBytes = (bitRate > 0 && totalDurationSeconds > 0)
        ? static_cast<uint64_t>(std::round((bitRate / 8.0) * totalDurationSeconds))
        : 0;

    auto startWall = std::chrono::steady_clock::now();
    auto lastCbTime = startWall;

    std::string lineBuffer;
    int ch;
    bool wasCancelled = false;

    while ((ch = fgetc(pipe)) != EOF) {
        if (cancellationToken && cancellationToken->load()) {
            wasCancelled = true;
            break;
        }

        if (ch == '\r' || ch == '\n') {
            if (!lineBuffer.empty()) {
                errorOutput += lineBuffer + "\n";

                if (lineBuffer.find("size=") != std::string::npos && lineBuffer.find("time=") != std::string::npos) {
                    auto now = std::chrono::steady_clock::now();
                    double elapsedMs = std::chrono::duration<double, std::milli>(now - lastCbTime).count();

                    if (elapsedMs >= 100.0) {
                        lastCbTime = now;

                        uint64_t currentBytes = 0;
                        std::smatch mSize;
                        if (std::regex_search(lineBuffer, mSize, std::regex("size=\\s*([0-9]+)kB"))) {
                            currentBytes = std::stoull(mSize[1].str()) * 1024;
                        }

                        double currentTimeSec = 0.0;
                        std::smatch mTime;
                        if (std::regex_search(lineBuffer, mTime, std::regex("time=([0-9]{2}):([0-9]{2}):([0-9]{2}\\.[0-9]+)"))) {
                            double h = std::stod(mTime[1].str());
                            double m = std::stod(mTime[2].str());
                            double s = std::stod(mTime[3].str());
                            currentTimeSec = (h * 3600.0) + (m * 60.0) + s;
                        }

                        double percent = 0.0;
                        if (totalDurationSeconds > 0.0 && currentTimeSec > 0.0) {
                            percent = std::min(100.0, (currentTimeSec / totalDurationSeconds) * 100.0);
                        }

                        double wallElapsedSec = std::chrono::duration<double>(now - startWall).count();
                        double speedBps = (wallElapsedSec > 0.1) ? (static_cast<double>(currentBytes) / wallElapsedSec) : 0.0;

                        if (progressCb) {
                            ProgressInfo info;
                            info.currentBytes = currentBytes;
                            info.totalBytes = expectedTotalBytes;
                            info.percent = percent;
                            info.speedBps = speedBps;
                            if (speedBps > 1024.0 && expectedTotalBytes > currentBytes) {
                                info.remainingSeconds = static_cast<uint64_t>((expectedTotalBytes - currentBytes) / speedBps);
                            }
                            info.statusMessage = "Extracting Audio";

                            bool keepGoing = progressCb(info);
                            if (!keepGoing) {
                                wasCancelled = true;
                                break;
                            }
                        }
                    }
                }
                lineBuffer.clear();
            }
        } else {
            lineBuffer += static_cast<char>(ch);
        }
    }

    int status = Platform::closeProcess(pipe);

    if (wasCancelled) {
        errorOutput = "Operation cancelled";
        std::error_code ec;
        fs::remove(outputPath, ec);
        return false;
    }

    std::error_code ec;
    return (status == 0) && fs::exists(outputPath, ec) && (fs::file_size(outputPath, ec) > 0);
}

DownloadResult AudioExtractor::extractAudioStream(
    const std::string& directAudioUrl,
    const std::string& fallbackVideoUrl,
    const StreamInfo& audioStream,
    const std::string& outputPath,
    double totalDurationSeconds,
    ProgressCallback progressCb,
    CancellationToken cancellationToken
) {
    DownloadResult result;
    result.path = outputPath;

    fs::path outP(outputPath);
    if (outP.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(outP.parent_path(), ec);
    }

    std::string err;
    bool ok = runFfmpegCommand(
        directAudioUrl,
        "",
        outputPath,
        totalDurationSeconds,
        audioStream.bitRate,
        err,
        progressCb,
        cancellationToken
    );

    if (!ok && (cancellationToken && cancellationToken->load())) {
        result.error = "Operation cancelled";
        return result;
    }

    if (!ok && !fallbackVideoUrl.empty()) {
        std::string mapArg = "0:" + std::to_string(audioStream.index);
        ok = runFfmpegCommand(
            fallbackVideoUrl,
            mapArg,
            outputPath,
            totalDurationSeconds,
            audioStream.bitRate,
            err,
            progressCb,
            cancellationToken
        );
    }

    std::error_code ec;
    if (ok && fs::exists(outputPath, ec)) {
        uint64_t size = fs::file_size(outputPath, ec);
        result.success = true;
        result.size = size;
        result.verified = true;

        if (progressCb) {
            ProgressInfo finished;
            finished.currentBytes = size;
            finished.totalBytes = size;
            finished.percent = 100.0;
            finished.statusMessage = "Completed";
            progressCb(finished);
        }
    } else {
        result.error = err.empty() ? "Audio extraction failed" : err;
    }

    return result;
}

} // namespace Emby
