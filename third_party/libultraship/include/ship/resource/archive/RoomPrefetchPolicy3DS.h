#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace Ship {
inline size_t RoomPrefetchBudget3DS(size_t freeBytes, bool oldModel, bool roomCached = false) {
    // A warm room can use its decoded resources immediately. Do not introduce
    // synchronous SD reads just to populate a cache it may never consume.
    if (roomCached) return 0;
    const size_t reserve = (oldModel ? 8U : 12U) * 1024U * 1024U;
    const size_t cap = oldModel ? 128U * 1024U : 2U * 1024U * 1024U;
    return freeBytes > reserve ? std::min(cap, freeBytes - reserve) : 0;
}

inline std::string RoomPrefix3DS(std::string_view path, bool masterQuest) {
    if (path.starts_with("__OTR__")) path.remove_prefix(7);
    if (path.starts_with("alt/")) path.remove_prefix(4);
    if (!path.starts_with("scenes/")) return {};
    const auto marker = path.rfind("_room_");
    if (marker == std::string_view::npos) return {};
    size_t end = marker + 6;
    while (end < path.size() && path[end] >= '0' && path[end] <= '9') ++end;
    if (end == marker + 6) return {};
    std::string prefix(path.substr(0, end));
    const auto nonmq = prefix.find("/nonmq/");
    if (masterQuest && nonmq != std::string::npos) prefix.replace(nonmq, 7, "/mq/");
    return prefix;
}
}
