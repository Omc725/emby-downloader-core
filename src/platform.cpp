#include "emby/platform.hpp"

#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
    #include <windows.h>
    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #include <unistd.h>
    #include <pwd.h>
    #define POPEN popen
    #define PCLOSE pclose
#endif

namespace Emby {
namespace Platform {

std::string getDefaultConfigDir() {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData) return std::string(appData) + "\\emby-downloader";
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) return std::string(userProfile) + "\\.config\\emby-downloader";
    return ".\\emby-downloader-config";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/Library/Application Support/emby-downloader";
    return "./.config/emby-downloader";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/emby-downloader";
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.config/emby-downloader";
    return "./.config/emby-downloader";
#endif
}

std::string getDefaultDownloadDir() {
#if defined(_WIN32)
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) return std::string(userProfile) + "\\Downloads";
    return ".\\downloads";
#else
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/Downloads";
    return "./downloads";
#endif
}

FILE* executeProcess(const std::string& command, const char* mode) {
    return POPEN(command.c_str(), mode);
}

int closeProcess(FILE* pipe) {
    if (!pipe) return -1;
    return PCLOSE(pipe);
}

std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        start++;
    }
    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(static_cast<unsigned char>(*end)));
    return std::string(start, end + 1);
}

std::string toLower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return res;
}

std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return escaped.str();
}

std::string formatBytes(uint64_t bytes) {
    if (bytes == 0) return "0 B";
    static const char* sizes[] = {"B", "KB", "MB", "GB", "TB"};
    double b = static_cast<double>(bytes);
    int i = 0;
    while (b >= 1024.0 && i < 4) {
        b /= 1024.0;
        i++;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << b << " " << sizes[i];
    return ss.str();
}

std::string sanitizeFilename(const std::string& name) {
    std::string sanitized = name;
    for (char& c : sanitized) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            c = '_';
        }
    }
    std::regex spaces("\\s+");
    sanitized = std::regex_replace(sanitized, spaces, " ");
    return trim(sanitized);
}

std::string cleanFilenameToTitle(const std::string& filename) {
    std::string s = filename;
    size_t lastSlash = s.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        s = s.substr(lastSlash + 1);
    }

    s = std::regex_replace(s, std::regex("\\.[a-zA-Z0-9]{2,4}$"), "");
    s = std::regex_replace(s, std::regex("\\[[^\\]]*\\]"), " ");
    s = std::regex_replace(s, std::regex("\\{[^}]*\\}"), " ");

    static const std::vector<std::string> patterns = {
        "\\b(2160p|1080p|1080i|720p|576p|480p|360p|4k|uhd|remux|bluray|blu-ray|web-dl|webrip|hdtv|dvdrip|brrip)\\b",
        "\\b(x264|x265|h264|h265|hevc|avc|hdr|hdr10|dv|dolby\\s*vision|truehd|atmos|dts|dts-hd|ac3|eac3|aac|mp3|flac)\\b",
        "\\b(s\\d{1,2}e\\d{1,2}|\\d{1,2}x\\d{1,2})\\b",
        "\\b(netflix|amazon|disney|apple|hbo)\\b",
        "#\\d+"
    };

    for (const auto& pat : patterns) {
        s = std::regex_replace(s, std::regex(pat, std::regex_constants::icase), " ");
    }

    s = std::regex_replace(s, std::regex("[._\\-+]"), " ");
    s = std::regex_replace(s, std::regex("\\s+"), " ");
    return trim(s);
}

std::string getAudioExtension(const std::string& codec) {
    if (codec.empty()) return "mka";
    std::string c = toLower(codec);
    if (c.find("eac3") != std::string::npos || c.find("ddp") != std::string::npos || c.find("ec-3") != std::string::npos) return "eac3";
    if (c.find("ac3") != std::string::npos || c.find("dolby digital") != std::string::npos) return "ac3";
    if (c.find("dts") != std::string::npos) return "dts";
    if (c.find("aac") != std::string::npos) return "m4a";
    if (c.find("mp3") != std::string::npos) return "mp3";
    if (c.find("flac") != std::string::npos) return "flac";
    if (c.find("opus") != std::string::npos) return "opus";
    if (c.find("vorbis") != std::string::npos || c.find("ogg") != std::string::npos) return "ogg";
    if (c.rfind("pcm", 0) == 0) return "wav";
    if (c == "truehd" || c == "thd") return "mka";
    return "mka";
}

bool checkFfmpegAvailable() {
#if defined(_WIN32)
    return (std::system("where ffmpeg > nul 2>&1") == 0);
#else
    return (std::system("ffmpeg -version > /dev/null 2>&1") == 0);
#endif
}

} // namespace Platform
} // namespace Emby
