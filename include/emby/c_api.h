#ifndef EMBY_C_API_H
#define EMBY_C_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef EMBY_CORE_EXPORTS
    #define EMBY_API __declspec(dllexport)
  #else
    #define EMBY_API __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define EMBY_API __attribute__((visibility("default")))
  #else
    #define EMBY_API
  #endif
#endif

typedef struct emby_engine_s emby_engine_t;

// Bellek yönetimi
EMBY_API void emby_string_free(char* str);

// Motor yaşam döngüsü
EMBY_API emby_engine_t* emby_engine_create(const char* config_path);
EMBY_API void emby_engine_destroy(emby_engine_t* engine);

// Kimlik doğrulama ve bağlantı
EMBY_API bool emby_engine_connect(
    emby_engine_t* engine,
    const char* server_url,
    const char* port,
    const char* username,
    const char* password,
    const char* prefix,
    char** out_error
);
EMBY_API bool emby_engine_test_connection(emby_engine_t* engine);
EMBY_API bool emby_engine_is_authenticated(emby_engine_t* engine);

// Profiller (JSON döner, emby_string_free ile serbest bırakılır)
EMBY_API char* emby_engine_get_profiles_json(emby_engine_t* engine);
EMBY_API bool emby_engine_load_profile(emby_engine_t* engine, const char* profile_id);

// Medya Arama ve Keşif (JSON döner)
EMBY_API char* emby_engine_search_json(emby_engine_t* engine, const char* query);
EMBY_API char* emby_engine_get_item_json(emby_engine_t* engine, const char* item_id);
EMBY_API char* emby_engine_get_seasons_json(emby_engine_t* engine, const char* series_id);
EMBY_API char* emby_engine_get_episodes_json(emby_engine_t* engine, const char* series_id, const char* season_id);

// Kuyruk Geri Çağırım Fonksiyon Göstericileri
typedef void (*emby_task_event_fn)(const char* task_id, const char* event_type, const char* payload_json, void* user_data);
typedef void (*emby_task_progress_fn)(const char* task_id, uint64_t current_bytes, uint64_t total_bytes, double speed_bps, double percent, void* user_data);

EMBY_API void emby_engine_set_queue_callbacks(
    emby_engine_t* engine,
    emby_task_event_fn on_event,
    emby_task_progress_fn on_progress,
    void* user_data
);

// Kuyruk Yönetimi
EMBY_API char* emby_engine_queue_add_video(
    emby_engine_t* engine,
    const char* item_json,
    const char* source_json,
    const char* output_dir
);
EMBY_API char* emby_engine_queue_add_audio(
    emby_engine_t* engine,
    const char* item_json,
    const char* source_json,
    const char* stream_json,
    const char* output_dir
);
EMBY_API char* emby_engine_queue_add_subtitle(
    emby_engine_t* engine,
    const char* item_json,
    const char* source_json,
    const char* stream_json,
    const char* output_dir
);

EMBY_API void emby_engine_queue_start(emby_engine_t* engine);
EMBY_API void emby_engine_queue_pause(emby_engine_t* engine);
EMBY_API void emby_engine_queue_resume(emby_engine_t* engine);
EMBY_API void emby_engine_queue_stop(emby_engine_t* engine);
EMBY_API void emby_engine_queue_cancel_task(emby_engine_t* engine, const char* task_id);
EMBY_API void emby_engine_queue_clear(emby_engine_t* engine);
EMBY_API char* emby_engine_queue_get_tasks_json(emby_engine_t* engine);

// Platform Bilgileri
EMBY_API char* emby_get_default_download_dir(void);
EMBY_API bool emby_is_ffmpeg_available(void);

#ifdef __cplusplus
}
#endif

#endif // EMBY_C_API_H
