// clangd_stubs.cpp
//
// Declarations for globals that live DIRECTLY in src/main.cpp (not pulled
// in via its #include chain). The actual Odin build is a unity build, so
// these definitions are in scope wherever any other src/*.cpp is parsed.
// clangd parses each file in isolation, so we declare them here as forced
// includes.
//
// IMPORTANT: Do NOT #include any other src/*.cpp from this file and do NOT
// provide actual definitions here. clangd only needs to see declarations
// for parsing; the real definitions come from main.cpp at link time.
//
// Keep this file in sync with the "loose" code at the top of src/main.cpp
// (everything before `#include "parser.hpp"` at main.cpp:68).

// Pull in gb.h for typedefs like isize, u8, etc. gb.h has include guards,
// so it's safe to be included again later via the unity chain.
#include "gb/gb.h"

// Forward decls of types whose full definition lives elsewhere in the chain.
struct ThreadPool;
struct Timings;
struct BlockingMutex;
// WorkerTaskProc is `typedef isize(void *) WorkerTaskProc` — see threading.cpp:42.
typedef isize WorkerTaskProc(void *);

// Globals defined in main.cpp. Declare (not define) so clangd sees the name.
// At link time main.cpp provides the actual definition.
extern Timings        global_timings;
extern BlockingMutex  debugf_mutex;
extern ThreadPool     global_thread_pool;

// Functions defined in main.cpp (loose, not via #include). Declare them so
// clangd sees the signatures. The real bodies are in main.cpp's TU.
gb_internal void init_global_thread_pool(void);
gb_internal void thread_pool_wait(void);
gb_internal bool thread_pool_add_task(WorkerTaskProc *proc, void *data);
gb_internal i64 PRINT_PEAK_USAGE(void);
gb_internal void debugf(char const *fmt, ...);
