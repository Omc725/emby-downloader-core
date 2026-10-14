#include "emby/client.hpp"
#include "emby/platform.hpp"

#include <curl/curl.h>
#include <mutex>
#include <set>
#include <algorithm>

namespace Emby {

static std::once_flag s_curlInitOnce;

static void ensureCurlInit() {
    std::call_once(s_curlInitOnce, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

static size_t writeStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(buffer, total);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = Platform::toLower(Platform::trim(line.substr(0, colon)));
        std::string val = Platform::trim(line.substr(colon + 1));
        (*headers)[key] = val;
    }
    return total;
}

static const std::string TMDB_API_KEY = "c153e7a274fbc502f641a2e88a38c20e";

EmbyClient::EmbyClient(
    const std::string& serverUrl,
    const std::string& port,
    const std::string& username,
    const std::string& prefix
) : m_serverUrl(normalizeUrl(serverUrl, port)),
    m_port(port),
    m_prefix(prefix.empty() ? "/emby" : (prefix[0] == '/' ? prefix : "/" + prefix)),
    m_username(username)
{
    ensureCurlInit();
}

void EmbyClient::setCredentials(const std::string& userId, const std::string& token) {
    m_userId = userId;
    m_token = token;
}

std::string EmbyClient::normalizeUrl(const std::string& url, const std::string& port) {
    if (url.empty()) return "";
    std::string u = Platform::trim(url);
    while (!u.empty() && u.back() == '/') {
        u.pop_back();
    }
    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) {
        u = "http://" + u;
    }
    if (!port.empty() && port != "80" && port != "443") {
        size_t colon = u.find(':', u.find("//") + 2);
        if (colon == std::string::npos) {
            u += ":" + port;
        }
    }
    return u;
}

std::map<std::string, std::string> EmbyClient::getHeaders() const {
    std::map<std::string, std::string> headers;
    headers["X-Emby-Authorization"] = "MediaBrowser Client=\"EmbyDownloaderCore\", Device=\"Desktop\", DeviceId=\"EmbyCore-01\", Version=\"1.0.0\"";
    if (!m_token.empty()) {
        headers["X-Emby-Token"] = m_token;
    }
    return headers;
}

HttpResponse EmbyClient::httpGet(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "CURL init failed";
        return resp;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse EmbyClient::httpPostJson(const std::string& url, const std::string& jsonBody, const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "CURL init failed";
        return resp;
    }

    struct curl_slist* chunk = nullptr;
    chunk = curl_slist_append(chunk, "Content-Type: application/json");
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse EmbyClient::httpHead(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "CURL init failed";
        return resp;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    return resp;
}

bool EmbyClient::authenticate(const std::string& password, std::string& errorMsg) {
    std::string authUrl = m_serverUrl + m_prefix + "/Users/AuthenticateByName";
    nlohmann::json body;
    body["Username"] = m_username;
    body["Pw"] = password;

    HttpResponse res = httpPostJson(authUrl, body.dump(), getHeaders());
    if (!res.isOk()) {
        errorMsg = "Authentication failed (HTTP " + std::to_string(res.statusCode) + "): " + res.error;
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(res.body);
        if (j.contains("AccessToken") && j["AccessToken"].is_string()) {
            m_token = j["AccessToken"].get<std::string>();
        }
        if (j.contains("User") && j["User"].contains("Id") && j["User"]["Id"].is_string()) {
            m_userId = j["User"]["Id"].get<std::string>();
        }

        if (m_token.empty() || m_userId.empty()) {
            errorMsg = "Invalid authentication response";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        errorMsg = std::string("JSON parsing error: ") + e.what();
        return false;
    }
}

bool EmbyClient::testConnection() {
    if (m_token.empty()) return false;
    std::string url = m_serverUrl + m_prefix + "/System/Info?api_key=" + Platform::urlEncode(m_token);
    HttpResponse res = httpGet(url, getHeaders());
    return res.isOk();
}

MediaItem EmbyClient::parseJsonToMediaItem(const nlohmann::json& j) {
    MediaItem item;
    if (!j.is_object()) return item;

    item.id = j.value("Id", "");
    item.name = j.value("Name", "");
    item.originalTitle = j.value("OriginalTitle", "");
    item.year = j.value("ProductionYear", 0);

    std::string typeStr = j.value("Type", "");
    if (typeStr == "Movie") {
        item.type = MediaType::Movie;
    } else if (typeStr == "Series") {
        item.type = MediaType::Series;
    } else if (typeStr == "Season") {
        item.type = MediaType::Season;
    } else if (typeStr == "Episode") {
        item.type = MediaType::Episode;
    } else {
        item.type = MediaType::Unknown;
    }

    item.seriesId = j.value("SeriesId", "");
    item.seriesName = j.value("SeriesName", "");
    item.seasonNumber = j.value("ParentIndexNumber", 0);
    item.episodeNumber = j.value("IndexNumber", 0);

    int64_t runTimeTicks = j.value("RunTimeTicks", 0LL);
    item.durationSeconds = static_cast<double>(runTimeTicks) / 10000000.0;

    if (j.contains("MediaSources") && j["MediaSources"].is_array()) {
        for (const auto& s : j["MediaSources"]) {
            MediaSource source;
            source.id = s.value("Id", "");
            source.name = s.value("Name", "");
            source.container = s.value("Container", "mkv");
            source.path = s.value("Path", "");
            source.size = s.value("Size", 0ULL);

            if (s.contains("MediaStreams") && s["MediaStreams"].is_array()) {
                for (const auto& st : s["MediaStreams"]) {
                    std::string streamType = st.value("Type", "");
                    if (streamType == "Video") {
                        source.width = st.value("Width", 0);
                        source.height = st.value("Height", 0);
                    } else if (streamType == "Audio") {
                        StreamInfo audio;
                        audio.index = st.value("Index", 0);
                        audio.type = "Audio";
                        audio.codec = st.value("Codec", "");
                        audio.language = st.value("Language", "");
                        audio.title = st.value("DisplayTitle", st.value("Title", ""));
                        audio.channels = st.value("ChannelLayout", "");
                        audio.bitRate = st.value("BitRate", 0);
                        audio.isDefault = st.value("IsDefault", false);
                        audio.isForced = st.value("IsForced", false);
                        source.audioStreams.push_back(audio);
                    } else if (streamType == "Subtitle") {
                        StreamInfo sub;
                        sub.index = st.value("Index", 0);
                        sub.type = "Subtitle";
                        sub.codec = st.value("Codec", "");
                        sub.language = st.value("Language", "");
                        sub.title = st.value("DisplayTitle", st.value("Title", ""));
                        sub.isDefault = st.value("IsDefault", false);
                        sub.isForced = st.value("IsForced", false);
                        source.subtitleStreams.push_back(sub);
                    }
                }
            }
            item.sources.push_back(source);
        }
    }

    return item;
}

std::vector<MediaItem> EmbyClient::searchByName(const std::string& query) {
    std::vector<MediaItem> results;
    std::set<std::string> seenIds;

    // 1. Direct search
    std::string url1 = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                       "/Items?SearchTerm=" + Platform::urlEncode(query) + 
                       "&IncludeItemTypes=Movie,Series&Recursive=true&Fields=MediaSources&api_key=" + Platform::urlEncode(m_token);
    HttpResponse res1 = httpGet(url1, getHeaders());
    if (res1.isOk()) {
        try {
            nlohmann::json data = nlohmann::json::parse(res1.body);
            if (data.contains("Items") && data["Items"].is_array()) {
                for (const auto& itemJson : data["Items"]) {
                    MediaItem item = parseJsonToMediaItem(itemJson);
                    if (!item.id.empty() && seenIds.find(item.id) == seenIds.end()) {
                        seenIds.insert(item.id);
                        results.push_back(item);
                    }
                }
            }
        } catch (...) {}
    }

    // 2. Cleaned title search
    std::string cleanedTitle = Platform::cleanFilenameToTitle(query);
    if (!cleanedTitle.empty() && Platform::toLower(cleanedTitle) != Platform::toLower(query)) {
        std::string url2 = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                           "/Items?SearchTerm=" + Platform::urlEncode(cleanedTitle) + 
                           "&IncludeItemTypes=Movie,Series&Recursive=true&Fields=MediaSources&api_key=" + Platform::urlEncode(m_token);
        HttpResponse res2 = httpGet(url2, getHeaders());
        if (res2.isOk()) {
            try {
                nlohmann::json data = nlohmann::json::parse(res2.body);
                if (data.contains("Items") && data["Items"].is_array()) {
                    for (const auto& itemJson : data["Items"]) {
                        MediaItem item = parseJsonToMediaItem(itemJson);
                        if (!item.id.empty() && seenIds.find(item.id) == seenIds.end()) {
                            seenIds.insert(item.id);
                            results.push_back(item);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // 3. Fallback search for episode parent series
    if (results.empty()) {
        std::string url3 = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                           "/Items?SearchTerm=" + Platform::urlEncode(query) + 
                           "&Recursive=true&Limit=10&Fields=MediaSources&api_key=" + Platform::urlEncode(m_token);
        HttpResponse res3 = httpGet(url3, getHeaders());
        if (res3.isOk()) {
            try {
                nlohmann::json data = nlohmann::json::parse(res3.body);
                if (data.contains("Items") && data["Items"].is_array()) {
                    for (const auto& itemJson : data["Items"]) {
                        std::string type = itemJson.value("Type", "");
                        std::string sId = itemJson.value("SeriesId", "");
                        std::string id = itemJson.value("Id", "");

                        if (type == "Episode" && !sId.empty() && seenIds.find(sId) == seenIds.end()) {
                            std::string sUrl = m_serverUrl + m_prefix + "/Users/" + m_userId + "/Items/" + sId + "?api_key=" + Platform::urlEncode(m_token);
                            HttpResponse sRes = httpGet(sUrl, getHeaders());
                            if (sRes.isOk()) {
                                nlohmann::json sData = nlohmann::json::parse(sRes.body);
                                MediaItem sItem = parseJsonToMediaItem(sData);
                                seenIds.insert(sId);
                                results.push_back(sItem);
                            }
                        } else if ((type == "Movie" || type == "Series") && !id.empty() && seenIds.find(id) == seenIds.end()) {
                            MediaItem item = parseJsonToMediaItem(itemJson);
                            seenIds.insert(id);
                            results.push_back(item);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return results;
}

nlohmann::json EmbyClient::fetchTmdbMetadata(const std::string& cleanId, bool isImdb) {
    nlohmann::json meta = nlohmann::json::object();
    try {
        if (isImdb) {
            std::string url = "https://api.themoviedb.org/3/find/" + cleanId + "?external_source=imdb_id&api_key=" + TMDB_API_KEY;
            HttpResponse res = httpGet(url);
            if (res.isOk()) {
                nlohmann::json data = nlohmann::json::parse(res.body);
                nlohmann::json match = nullptr;
                if (data.contains("movie_results") && !data["movie_results"].empty()) {
                    match = data["movie_results"][0];
                } else if (data.contains("tv_results") && !data["tv_results"].empty()) {
                    match = data["tv_results"][0];
                }
                if (match != nullptr && match.is_object()) {
                    meta["title"] = match.value("title", match.value("name", ""));
                }
            }
        } else {
            std::string mUrl = "https://api.themoviedb.org/3/movie/" + cleanId + "?api_key=" + TMDB_API_KEY;
            HttpResponse mRes = httpGet(mUrl);
            if (mRes.isOk()) {
                nlohmann::json data = nlohmann::json::parse(mRes.body);
                meta["title"] = data.value("title", data.value("name", ""));
            } else {
                std::string tUrl = "https://api.themoviedb.org/3/tv/" + cleanId + "?api_key=" + TMDB_API_KEY;
                HttpResponse tRes = httpGet(tUrl);
                if (tRes.isOk()) {
                    nlohmann::json data = nlohmann::json::parse(tRes.body);
                    meta["title"] = data.value("name", "");
                }
            }
        }
    } catch (...) {}
    return meta;
}

std::vector<MediaItem> EmbyClient::searchById(const std::string& idInput) {
    std::string raw = Platform::trim(idInput);
    bool isImdb = false;
    std::string cleanId = raw;

    if (raw.rfind("tt", 0) == 0 || raw.rfind("TT", 0) == 0) {
        isImdb = true;
        cleanId = Platform::toLower(raw);
    } else if (Platform::toLower(raw).rfind("imdb:", 0) == 0) {
        isImdb = true;
        cleanId = Platform::toLower(Platform::trim(raw.substr(5)));
    } else if (Platform::toLower(raw).rfind("tmdb:", 0) == 0) {
        isImdb = false;
        cleanId = Platform::trim(raw.substr(5));
    } else {
        bool allDigits = !raw.empty() && std::all_of(raw.begin(), raw.end(), ::isdigit);
        if (allDigits) {
            isImdb = false;
            cleanId = raw;
        }
    }

    std::vector<std::string> providerQueries;
    if (isImdb) {
        providerQueries.push_back("imdb." + cleanId + ",Imdb." + cleanId);
        providerQueries.push_back("tmdb." + cleanId + ",Tmdb." + cleanId);
    } else {
        providerQueries.push_back("tmdb." + cleanId + ",Tmdb." + cleanId);
        providerQueries.push_back("TheMovieDb." + cleanId);
    }

    std::vector<MediaItem> results;
    std::set<std::string> seenIds;

    for (const auto& q : providerQueries) {
        std::string url = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                          "/Items?AnyProviderIdEquals=" + Platform::urlEncode(q) + 
                          "&IncludeItemTypes=Movie,Series&Recursive=true&Fields=MediaSources&api_key=" + Platform::urlEncode(m_token);
        HttpResponse res = httpGet(url, getHeaders());
        if (res.isOk()) {
            try {
                nlohmann::json data = nlohmann::json::parse(res.body);
                if (data.contains("Items") && data["Items"].is_array()) {
                    for (const auto& itemJson : data["Items"]) {
                        MediaItem item = parseJsonToMediaItem(itemJson);
                        if (!item.id.empty() && seenIds.find(item.id) == seenIds.end()) {
                            seenIds.insert(item.id);
                            results.push_back(item);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    if (results.empty()) {
        nlohmann::json meta = fetchTmdbMetadata(cleanId, isImdb);
        if (meta.contains("title") && meta["title"].is_string()) {
            std::string title = meta["title"].get<std::string>();
            return searchByName(title);
        }
    }

    return results;
}

MediaItem EmbyClient::getItem(const std::string& itemId) {
    std::string url = m_serverUrl + m_prefix + "/Users/" + m_userId + "/Items/" + itemId + "?Fields=MediaSources&api_key=" + Platform::urlEncode(m_token);
    HttpResponse res = httpGet(url, getHeaders());
    if (res.isOk()) {
        try {
            nlohmann::json data = nlohmann::json::parse(res.body);
            MediaItem item = parseJsonToMediaItem(data);

            // Eğer MediaSources boşsa veya tek bir kaynak dönmüşse PlaybackInfo ile ek kaynakları kontrol et
            if (item.sources.empty() || item.sources.size() <= 1) {
                std::string pUrl = m_serverUrl + m_prefix + "/Items/" + itemId + "/PlaybackInfo?UserId=" + m_userId + "&api_key=" + Platform::urlEncode(m_token);
                HttpResponse pRes = httpPostJson(pUrl, "{}", getHeaders());
                if (pRes.isOk()) {
                    nlohmann::json pData = nlohmann::json::parse(pRes.body);
                    if (pData.contains("MediaSources") && pData["MediaSources"].is_array()) {
                        nlohmann::json wrapper = data;
                        wrapper["MediaSources"] = pData["MediaSources"];
                        MediaItem playItem = parseJsonToMediaItem(wrapper);
                        if (playItem.sources.size() > item.sources.size()) {
                            item.sources = std::move(playItem.sources);
                        }
                    }
                }
            }

            return item;
        } catch (...) {}
    }
    return MediaItem();
}

std::vector<MediaItem> EmbyClient::getSeasons(const std::string& seriesId) {
    std::vector<MediaItem> results;
    std::string url = m_serverUrl + m_prefix + "/Shows/" + seriesId + "/Seasons?UserId=" + m_userId + "&api_key=" + Platform::urlEncode(m_token);
    HttpResponse res = httpGet(url, getHeaders());
    if (res.isOk()) {
        try {
            nlohmann::json data = nlohmann::json::parse(res.body);
            if (data.contains("Items") && data["Items"].is_array()) {
                for (const auto& itemJson : data["Items"]) {
                    results.push_back(parseJsonToMediaItem(itemJson));
                }
            }
        } catch (...) {}
    }
    return results;
}

std::vector<MediaItem> EmbyClient::getEpisodes(const std::string& seriesId, const std::string& seasonId) {
    std::vector<MediaItem> results;
    std::string sQuery = seasonId.empty() ? "" : ("SeasonId=" + Platform::urlEncode(seasonId) + "&");
    std::string url = m_serverUrl + m_prefix + "/Shows/" + seriesId + "/Episodes?" + sQuery + "UserId=" + m_userId + "&Fields=MediaSources&api_key=" + Platform::urlEncode(m_token);
    HttpResponse res = httpGet(url, getHeaders());
    if (res.isOk()) {
        try {
            nlohmann::json data = nlohmann::json::parse(res.body);
            if (data.contains("Items") && data["Items"].is_array()) {
                for (const auto& itemJson : data["Items"]) {
                    results.push_back(parseJsonToMediaItem(itemJson));
                }
            }
        } catch (...) {}
    }
    return results;
}

std::string EmbyClient::buildDirectVideoUrl(const std::string& itemId, const MediaSource& source) const {
    std::string container = source.container.empty() ? "mkv" : Platform::toLower(source.container);
    return m_serverUrl + m_prefix + "/Videos/" + itemId + "/stream." + container + 
           "?static=true&MediaSourceId=" + Platform::urlEncode(source.id) + 
           "&EnableDirectPlay=true&EnableDirectStream=true&AllowVideoStreamCopy=true&AllowAudioStreamCopy=true&api_key=" + Platform::urlEncode(m_token);
}

std::string EmbyClient::buildDirectAudioStreamUrl(const std::string& itemId, const MediaSource& source, const StreamInfo& audioStream) const {
    return m_serverUrl + m_prefix + "/Audio/" + itemId + "/stream.mka?AudioStreamIndex=" + std::to_string(audioStream.index) + 
           "&MediaSourceId=" + Platform::urlEncode(source.id) + "&api_key=" + Platform::urlEncode(m_token);
}

std::string EmbyClient::getSubtitleUrl(const std::string& itemId, const std::string& sourceId, int subIndex, const std::string& ext) const {
    return m_serverUrl + m_prefix + "/Videos/" + itemId + "/" + sourceId + "/Subtitles/" + std::to_string(subIndex) + 
           "/Stream." + ext + "?api_key=" + Platform::urlEncode(m_token);
}

std::string EmbyClient::getOfficialDownloadUrl(const std::string& itemId) const {
    return m_serverUrl + m_prefix + "/Items/" + itemId + "/Download?api_key=" + Platform::urlEncode(m_token);
}

bool EmbyClient::checkOfficialDownloadAvailable(const std::string& itemId) {
    std::string url = getOfficialDownloadUrl(itemId);
    HttpResponse res = httpHead(url, getHeaders());
    return (res.statusCode >= 200 && res.statusCode < 400);
}

} // namespace Emby
