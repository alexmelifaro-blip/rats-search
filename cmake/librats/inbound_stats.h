#pragma once

/*
 * Rolling per-address tally of inbound connection handshakes.
 *
 * Every inbound connection starts a Noise handshake as the responder. Logging
 * each one individually can flood the log (tens of handshakes per second from
 * peers that never finish them), so this module only counts: handshakes started
 * and completed, per remote IP. Once per interval it writes a single summary line
 * under the "inbound" log tag with the totals and the busiest addresses, then
 * starts a new window.
 *
 * Thread-safe: reactors run on several threads and all report here.
 */

#include "librats/core/address.h"
#include "librats/util/logger.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace librats::inbound_stats {

inline constexpr std::chrono::seconds kInterval{60};
inline constexpr std::size_t kTopAddresses = 10;
// Bounds memory under a spray from many distinct addresses: once this many are
// tracked in one window, new ones still count towards the totals but get no
// per-address entry.
inline constexpr std::size_t kMaxTrackedAddresses = 50000;

struct Counts {
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
};

struct State {
    std::mutex mutex;
    std::unordered_map<std::string, Counts> by_address;
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
    std::chrono::steady_clock::time_point window_start = std::chrono::steady_clock::now();
};

inline State& state() {
    static State s;
    return s;
}

inline std::string key_for(const std::optional<Address>& remote) {
    // The port is left out on purpose: a host retrying from fresh source ports
    // is still one host.
    return remote ? remote->ip.to_string() : std::string("(relayed)");
}

// Caller holds s.mutex.
inline void flush_window(State& s, std::chrono::steady_clock::time_point now) {
    if (s.started != 0 || s.completed != 0) {
        std::vector<std::pair<std::string, Counts>> top(s.by_address.begin(), s.by_address.end());
        const std::size_t shown = (std::min)(kTopAddresses, top.size());
        std::partial_sort(top.begin(), top.begin() + static_cast<std::ptrdiff_t>(shown), top.end(),
                          [](const auto& a, const auto& b) { return a.second.started > b.second.started; });

        std::ostringstream line;
        line << "handshakes in last "
             << std::chrono::duration_cast<std::chrono::seconds>(now - s.window_start).count() << "s: "
             << s.started << " started, " << s.completed << " completed, from " << s.by_address.size()
             << " address(es)";
        if (shown > 0) {
            line << "; busiest (started/completed):";
            for (std::size_t i = 0; i < shown; ++i)
                line << ' ' << top[i].first << '=' << top[i].second.started << '/' << top[i].second.completed;
        }
        LOG_INFO("inbound", line.str());
    }

    s.by_address.clear();
    s.started = 0;
    s.completed = 0;
    s.window_start = now;
}

inline void record(const std::optional<Address>& remote, bool completed) {
    State& s = state();
    const auto now = std::chrono::steady_clock::now();
    const std::string key = key_for(remote);

    std::lock_guard<std::mutex> lock(s.mutex);
    if (completed) ++s.completed;
    else           ++s.started;

    auto it = s.by_address.find(key);
    if (it == s.by_address.end() && s.by_address.size() < kMaxTrackedAddresses)
        it = s.by_address.emplace(key, Counts{}).first;
    if (it != s.by_address.end()) {
        if (completed) ++it->second.completed;
        else           ++it->second.started;
    }

    if (now - s.window_start >= kInterval) flush_window(s, now);
}

/// An inbound handshake has begun (we are the Noise responder).
inline void handshake_started(const std::optional<Address>& remote) { record(remote, false); }

/// An inbound handshake finished successfully.
inline void handshake_completed(const std::optional<Address>& remote) { record(remote, true); }

} // namespace librats::inbound_stats
