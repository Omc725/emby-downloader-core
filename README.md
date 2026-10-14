# emby-downloader-core

Cross-platform C++ core engine for Emby media discovery, bit-for-bit direct video acquisition, stream-copy audio extraction, and asynchronous queue management. Designed as a headless backend for desktop graphical user interfaces (Qt, Flutter, Tauri, Electron, GTK, .NET).

## Architecture

The engine is completely decoupled from any terminal or console interface. It exposes event-driven progress reporting, cooperative cancellation tokens, and thread-safe queue management via both a modern C++ facade and a standard C ABI (`c_api.h`).

### Components

- `EmbyEngine`: Unified application facade combining profile management, REST communication, direct downloading, audio extraction, and queue dispatching.
- `EmbyClient`: Typed HTTP client for Emby server authentication, media discovery, hierarchical navigation (Series -> Seasons -> Episodes), and TMDb fallback querying.
- `Downloader`: Bit-for-bit direct video stream retrieval with HTTP Range resume support (`.part` file recovery) and subtitle downloading. No transcoding is applied.
- `AudioExtractor`: Headless FFmpeg runner extracting target audio tracks via bitstream copy (`-c:a copy`) with progress metric parsing.
- `QueueManager`: Multi-threaded task scheduler supporting pause, resume, individual task cancellation, and JSON persistence.
- `Platform`: OS-agnostic path resolvers (Windows `%APPDATA%`, macOS `~/Library/Application Support`, Linux `$XDG_CONFIG_HOME`) and cross-platform process execution.
- `c_api.h`: Pure C ABI interface for FFI bindings in Flutter (Dart FFI), Tauri (Rust bindgen), Electron (Node-API), and .NET.

## Dependencies

- C++17 compatible compiler (GCC >= 9, Clang >= 10, MSVC >= 2019)
- `libcurl` (development package)
- `pthread` (POSIX threads)
- `ffmpeg` runtime binary (required only for audio track extraction)

## Build Instructions

### Using CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Targets generated:
- `libemby_core.so` / `emby_core.dll` / `libemby_core.dylib` (Shared library)
- `libemby_core.a` / `emby_core.lib` (Static library)
- `headless_demo` (Headless test harness)

### Using Make

```bash
make
./headless_demo
```

## C ABI Integration

The C interface defined in `include/emby/c_api.h` enables integration with any language runtime supporting dynamic library loading:

```c
#include "emby/c_api.h"

emby_engine_t* engine = emby_engine_create(NULL);
char* results_json = emby_engine_search_json(engine, "Inception");
// Consume JSON in UI layer...
emby_string_free(results_json);
emby_engine_destroy(engine);
```

## License

MIT License. See `LICENSE` for details.
