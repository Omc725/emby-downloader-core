#include "emby/c_api.h"
#include "emby/engine.hpp"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>

struct emby_engine_s {
    std::unique_ptr<Emby::EmbyEngine> engine;
    emby_task_event_fn on_event = nullptr;
    emby_task_progress_fn on_progress = nullptr;
    void* user_data = nullptr;
};

static char* duplicateString(const std::string& str) {
    char* copy = static_cast<char*>(std::malloc(str.size() + 1));
    if (copy) {
        std::memcpy(copy, str.c_str(), str.size() + 1);
    }
    return copy;
}

static nlohmann::json mediaItemToJson(const Emby::MediaItem& item) {
    nlohmann::json j;
    j["id"] = item.id;
    j["name"] = item.name;
    j["originalTitle"] = item.originalTitle;
    j["year"] = item.year;
    j["seriesId"] = item.seriesId;
    j["seriesName"] = item.seriesName;
    j["seasonNumber"] = item.seasonNumber;
    j["episodeNumber"] = item.episodeNumber;
    j["durationSeconds"] = item.durationSeconds;

    std::string typeStr = "Unknown";
    switch (item.type) {
        case Emby::MediaType::Movie: typeStr = "Movie"; break;
        case Emby::MediaType::Series: typeStr = "Series"; break;
        case Emby::MediaType::Season: typeStr = "Season"; break;
        case Emby::MediaType::Episode: typeStr = "Episode"; break;
        default: break;
    }
    j["type"] = typeStr;

    nlohmann::json sources = nlohmann::json::array();
    for (const auto& s : item.sources) {
        nlohmann::json sj;
        sj["id"] = s.id;
        sj["name"] = s.name;
        sj["container"] = s.container;
        sj["size"] = s.size;
        sj["width"] = s.width;
        sj["height"] = s.height;

        nlohmann::json audios = nlohmann::json::array();
        for (const auto& a : s.audioStreams) {
            nlohmann::json aj;
            aj["index"] = a.index;
            aj["codec"] = a.codec;
            aj["language"] = a.language;
            aj["title"] = a.title;
            aj["channels"] = a.channels;
            aj["bitRate"] = a.bitRate;
            aj["isDefault"] = a.isDefault;
            audios.push_back(aj);
        }
        sj["audioStreams"] = audios;

        nlohmann::json subs = nlohmann::json::array();
        for (const auto& sub : s.subtitleStreams) {
            nlohmann::json subj;
            subj["index"] = sub.index;
            subj["codec"] = sub.codec;
            subj["language"] = sub.language;
            subj["title"] = sub.title;
            subj["isDefault"] = sub.isDefault;
            subs.push_back(subj);
        }
        sj["subtitleStreams"] = subs;

        sources.push_back(sj);
    }
    j["sources"] = sources;

    return j;
}

static Emby::MediaItem jsonToMediaItem(const nlohmann::json& j) {
    return Emby::EmbyClient::parseJsonToMediaItem(j);
}

extern "C" {

void emby_string_free(char* str) {
    if (str) {
        std::free(str);
    }
}

emby_engine_t* emby_engine_create(const char* config_path) {
    auto handle = new emby_engine_s();
    std::string path = config_path ? config_path : "";
    handle->engine = std::make_unique<Emby::EmbyEngine>(path);
    return handle;
}

void emby_engine_destroy(emby_engine_t* engine) {
    if (engine) {
        delete engine;
    }
}

bool emby_engine_connect(
    emby_engine_t* engine,
    const char* server_url,
    const char* port,
    const char* username,
    const char* password,
    const char* prefix,
    char** out_error
) {
    if (!engine || !engine->engine) return false;
    std::string err;
    std::string p = prefix ? prefix : "/emby";
    bool ok = engine->engine->connect(
        server_url ? server_url : "",
        port ? port : "443",
        username ? username : "",
        password ? password : "",
        err,
        p
    );
    if (!ok && out_error) {
        *out_error = duplicateString(err);
    }
    return ok;
}

bool emby_engine_test_connection(emby_engine_t* engine) {
    if (!engine || !engine->engine) return false;
    return engine->engine->testConnection();
}

bool emby_engine_is_authenticated(emby_engine_t* engine) {
    if (!engine || !engine->engine) return false;
    return engine->engine->isAuthenticated();
}

char* emby_engine_get_profiles_json(emby_engine_t* engine) {
    if (!engine || !engine->engine) return duplicateString("[]");
    auto profiles = engine->engine->getConfig().getProfiles();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : profiles) {
        nlohmann::json pj;
        pj["id"] = p.id;
        pj["name"] = p.name;
        pj["serverUrl"] = p.serverUrl;
        pj["port"] = p.port;
        pj["prefix"] = p.prefix;
        pj["username"] = p.username;
        pj["userId"] = p.userId;
        pj["token"] = p.token;
        j.push_back(pj);
    }
    return duplicateString(j.dump());
}

bool emby_engine_load_profile(emby_engine_t* engine, const char* profile_id) {
    if (!engine || !engine->engine || !profile_id) return false;
    return engine->engine->loadProfile(profile_id);
}

char* emby_engine_search_json(emby_engine_t* engine, const char* query) {
    if (!engine || !engine->engine || !query) return duplicateString("[]");
    auto items = engine->engine->search(query);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& item : items) {
        j.push_back(mediaItemToJson(item));
    }
    return duplicateString(j.dump());
}

char* emby_engine_get_item_json(emby_engine_t* engine, const char* item_id) {
    if (!engine || !engine->engine || !item_id) return duplicateString("{}");
    auto item = engine->engine->getItem(item_id);
    return duplicateString(mediaItemToJson(item).dump());
}

char* emby_engine_get_seasons_json(emby_engine_t* engine, const char* series_id) {
    if (!engine || !engine->engine || !series_id) return duplicateString("[]");
    auto items = engine->engine->getSeasons(series_id);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& item : items) {
        j.push_back(mediaItemToJson(item));
    }
    return duplicateString(j.dump());
}

char* emby_engine_get_episodes_json(emby_engine_t* engine, const char* series_id, const char* season_id) {
    if (!engine || !engine->engine || !series_id) return duplicateString("[]");
    std::string sId = season_id ? season_id : "";
    auto items = engine->engine->getEpisodes(series_id, sId);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& item : items) {
        j.push_back(mediaItemToJson(item));
    }
    return duplicateString(j.dump());
}

void emby_engine_set_queue_callbacks(
    emby_engine_t* engine,
    emby_task_event_fn on_event,
    emby_task_progress_fn on_progress,
    void* user_data
) {
    if (!engine || !engine->engine) return;
    engine->on_event = on_event;
    engine->on_progress = on_progress;
    engine->user_data = user_data;

    auto& q = engine->engine->getQueue();

    q.setOnTaskStarted([engine](const Emby::QueueTask& task) {
        if (engine->on_event) {
            engine->on_event(task.id.c_str(), "started", task.name.c_str(), engine->user_data);
        }
    });

    q.setOnTaskProgress([engine](const Emby::QueueTask& task, const Emby::ProgressInfo& progress) {
        if (engine->on_progress) {
            engine->on_progress(task.id.c_str(), progress.currentBytes, progress.totalBytes, progress.speedBps, progress.percent, engine->user_data);
        }
    });

    q.setOnTaskCompleted([engine](const Emby::QueueTask& task, const Emby::DownloadResult& result) {
        if (engine->on_event) {
            engine->on_event(task.id.c_str(), "completed", result.path.c_str(), engine->user_data);
        }
    });

    q.setOnTaskError([engine](const Emby::QueueTask& task, const std::string& error) {
        if (engine->on_event) {
            engine->on_event(task.id.c_str(), "error", error.c_str(), engine->user_data);
        }
    });

    q.setOnQueueFinished([engine]() {
        if (engine->on_event) {
            engine->on_event("", "queue_finished", "", engine->user_data);
        }
    });
}

char* emby_engine_queue_add_video(
    emby_engine_t* engine,
    const char* item_json,
    const char* source_json,
    const char* output_dir
) {
    if (!engine || !engine->engine || !item_json || !source_json) return duplicateString("");
    try {
        nlohmann::json ij = nlohmann::json::parse(item_json);
        nlohmann::json sj = nlohmann::json::parse(source_json);

        Emby::MediaItem item = jsonToMediaItem(ij);
        Emby::MediaSource source;
        source.id = sj.value("id", "");
        source.name = sj.value("name", "");
        source.container = sj.value("container", "mkv");
        source.size = sj.value("size", 0ULL);

        std::string dir = output_dir ? output_dir : "";
        std::string id = engine->engine->enqueueVideo(item, source, dir);
        return duplicateString(id);
    } catch (...) {
        return duplicateString("");
    }
}

char* emby_engine_queue_add_audio(
    emby_engine_t* engine,
    const char* item_json,
    const char* source_json,
    const char* stream_json,
    const char* output_dir
) {
    if (!engine || !engine->engine || !item_json || !source_json || !stream_json) return duplicateString("");
    try {
        nlohmann::json ij = nlohmann::json::parse(item_json);
        nlohmann::json sj = nlohmann::json::parse(source_json);
        nlohmann::json stj = nlohmann::json::parse(stream_json);

        Emby::MediaItem item = jsonToMediaItem(ij);
        Emby::MediaSource source;
        source.id = sj.value("id", "");
        source.name = sj.value("name", "");
        source.container = sj.value("container", "mkv");

        Emby::StreamInfo stream;
        stream.index = stj.value("index", 0);
        stream.codec = stj.value("codec", "");
        stream.language = stj.value("language", "");

        std::string dir = output_dir ? output_dir : "";
        std::string id = engine->engine->enqueueAudio(item, source, stream, dir);
        return duplicateString(id);
    } catch (...) {
        return duplicateString("");
    }
}

char* emby_engine_queue_add_subtitle(
    emby_engine_t* engine,
    const char* item_json,
    const char* source_json,
    const char* stream_json,
    const char* output_dir
) {
    if (!engine || !engine->engine || !item_json || !source_json || !stream_json) return duplicateString("");
    try {
        nlohmann::json ij = nlohmann::json::parse(item_json);
        nlohmann::json sj = nlohmann::json::parse(source_json);
        nlohmann::json stj = nlohmann::json::parse(stream_json);

        Emby::MediaItem item = jsonToMediaItem(ij);
        Emby::MediaSource source;
        source.id = sj.value("id", "");
        source.name = sj.value("name", "");
        source.container = sj.value("container", "mkv");

        Emby::StreamInfo stream;
        stream.index = stj.value("index", 0);
        stream.codec = stj.value("codec", "");
        stream.language = stj.value("language", "");

        std::string dir = output_dir ? output_dir : "";
        std::string id = engine->engine->enqueueSubtitle(item, source, stream, dir);
        return duplicateString(id);
    } catch (...) {
        return duplicateString("");
    }
}

void emby_engine_queue_start(emby_engine_t* engine) {
    if (engine && engine->engine) {
        engine->engine->getQueue().start();
    }
}

void emby_engine_queue_pause(emby_engine_t* engine) {
    if (engine && engine->engine) {
        engine->engine->getQueue().pause();
    }
}

void emby_engine_queue_resume(emby_engine_t* engine) {
    if (engine && engine->engine) {
        engine->engine->getQueue().resume();
    }
}

void emby_engine_queue_stop(emby_engine_t* engine) {
    if (engine && engine->engine) {
        engine->engine->getQueue().stop();
    }
}

void emby_engine_queue_cancel_task(emby_engine_t* engine, const char* task_id) {
    if (engine && engine->engine && task_id) {
        engine->engine->getQueue().cancelTask(task_id);
    }
}

void emby_engine_queue_clear(emby_engine_t* engine) {
    if (engine && engine->engine) {
        engine->engine->getQueue().clear();
    }
}

char* emby_engine_queue_get_tasks_json(emby_engine_t* engine) {
    if (!engine || !engine->engine) return duplicateString("[]");
    auto tasks = engine->engine->getQueue().getTasks();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& t : tasks) {
        nlohmann::json tj;
        tj["id"] = t.id;
        tj["name"] = t.name;
        tj["outputPath"] = t.outputPath;
        tj["expectedSize"] = t.expectedSize;
        tj["error"] = t.error;

        std::string statusStr = "pending";
        switch (t.status) {
            case Emby::TaskStatus::Downloading: statusStr = "downloading"; break;
            case Emby::TaskStatus::Completed: statusStr = "completed"; break;
            case Emby::TaskStatus::Error: statusStr = "error"; break;
            case Emby::TaskStatus::Cancelled: statusStr = "cancelled"; break;
            default: break;
        }
        tj["status"] = statusStr;

        nlohmann::json pj;
        pj["currentBytes"] = t.progress.currentBytes;
        pj["totalBytes"] = t.progress.totalBytes;
        pj["percent"] = t.progress.percent;
        pj["speedBps"] = t.progress.speedBps;
        pj["remainingSeconds"] = t.progress.remainingSeconds;
        pj["statusMessage"] = t.progress.statusMessage;
        tj["progress"] = pj;

        j.push_back(tj);
    }
    return duplicateString(j.dump());
}

char* emby_get_default_download_dir(void) {
    return duplicateString(Emby::Platform::getDefaultDownloadDir());
}

bool emby_is_ffmpeg_available(void) {
    return Emby::Platform::checkFfmpegAvailable();
}

} // extern "C"
