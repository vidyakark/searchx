#pragma once
#include <string>

#include "searchx/engine.hpp"

// Minimal multi-threaded HTTP/1.1 server (POSIX sockets). Blocks forever.
//   GET /            web UI
//   GET /search?q=..&k=..   JSON results
//   GET /stats       index + cache statistics
int runServer(const searchx::Engine& engine, const std::string& host, int port, int threads);
