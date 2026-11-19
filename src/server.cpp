#include "server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>
#include <vector>

#include "ui.hpp"

using searchx::Engine;

namespace {

std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else o += s[i];
    }
    return o;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    return o;
}

std::map<std::string, std::string> parseParams(const std::string& qs) {
    std::map<std::string, std::string> m;
    size_t i = 0;
    while (i <= qs.size()) {
        size_t amp = qs.find('&', i);
        if (amp == std::string::npos) amp = qs.size();
        std::string kv = qs.substr(i, amp - i);
        size_t eq = kv.find('=');
        if (eq != std::string::npos) m[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
        i = amp + 1;
    }
    return m;
}

void respond(int fd, int status, const char* type, const std::string& body) {
    const char* text = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Bad Request";
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: " + type +
                       "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    std::string all = head + body;
    size_t off = 0;
    while (off < all.size()) {
        ssize_t n = send(fd, all.data() + off, all.size() - off, MSG_NOSIGNAL);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
}

void handle(int fd, const Engine& eng) {
    timeval tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
    }
    size_t sp1 = req.find(' '), sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return respond(fd, 400, "text/plain", "bad request");
    if (req.compare(0, sp1, "GET") != 0) return respond(fd, 405, "text/plain", "GET only");

    std::string target = req.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t qm = target.find('?');
    std::string path = target.substr(0, qm);
    auto params = parseParams(qm == std::string::npos ? "" : target.substr(qm + 1));

    if (path == "/") return respond(fd, 200, "text/html; charset=utf-8", kIndexHtml);

    if (path == "/search") {
        size_t k = 10;
        if (params.count("k")) k = std::min<size_t>(100, std::max<long>(1, std::atol(params["k"].c_str())));
        searchx::SearchResult r = eng.search(params["q"], k);
        const searchx::Index& ix = eng.index();
        std::string j = "{\"query\":\"" + jsonEscape(params["q"]) + "\",\"total\":" + std::to_string(r.totalMatches) +
                        ",\"tookMs\":" + std::to_string(r.micros / 1000.0) + ",\"cached\":" + (r.cached ? "true" : "false") +
                        ",\"results\":[";
        for (size_t i = 0; i < r.hits.size(); ++i) {
            const auto& h = r.hits[i];
            if (i) j += ',';
            j += "{\"id\":" + std::to_string(h.doc) + ",\"score\":" + std::to_string(h.score) + ",\"title\":\"" +
                 jsonEscape(ix.title(h.doc)) + "\",\"snippet\":\"" + jsonEscape(ix.snippet(h.doc)) + "\"}";
        }
        return respond(fd, 200, "application/json", j + "]}");
    }

    if (path == "/stats") {
        const searchx::Index& ix = eng.index();
        std::string j = "{\"docs\":" + std::to_string(ix.numDocs()) + ",\"terms\":" + std::to_string(ix.numTerms()) +
                        ",\"postingsBytes\":" + std::to_string(ix.postingsBytes()) +
                        ",\"cacheHits\":" + std::to_string(eng.cacheHits()) +
                        ",\"cacheMisses\":" + std::to_string(eng.cacheMisses()) + "}";
        return respond(fd, 200, "application/json", j);
    }
    respond(fd, 404, "text/plain", "not found");
}

}  // namespace

int runServer(const Engine& engine, const std::string& host, int port, int threads) {
    std::signal(SIGPIPE, SIG_IGN);
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { std::perror("socket"); return 1; }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) { std::fprintf(stderr, "bad host %s\n", host.c_str()); return 1; }
    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) { std::perror("bind"); return 1; }
    if (listen(ls, 512) != 0) { std::perror("listen"); return 1; }
    std::printf("searchx listening on http://%s:%d  (%d worker threads)\n", host.c_str(), port, threads);
    std::fflush(stdout);

    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i)
        pool.emplace_back([&] {
            for (;;) {
                int fd = accept(ls, nullptr, nullptr);
                if (fd < 0) continue;
                handle(fd, engine);
                close(fd);
            }
        });
    for (auto& t : pool) t.join();
    return 0;
}
