#pragma once

#include <string>
#include <cstdint>
#include <cstdio>

namespace Emby {
namespace Platform {

std::string getDefaultConfigDir();
std::string getDefaultDownloadDir();

FILE* executeProcess(const std::string& command, const char* mode);
int closeProcess(FILE* pipe);

std::string formatBytes(uint64_t bytes);
std::string sanitizeFilename(const std::string& name);
std::string cleanFilenameToTitle(const std::string& filename);
std::string getAudioExtension(const std::string& codec);
std::string urlEncode(const std::string& value);
std::string trim(const std::string& s);
std::string toLower(const std::string& s);

bool checkFfmpegAvailable();

} // namespace Platform
} // namespace Emby
