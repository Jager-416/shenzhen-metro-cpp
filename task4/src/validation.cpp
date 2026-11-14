#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <regex>

namespace metro {
void validate_keys(const Json &body, const std::vector<std::string> &allowed) {
    if (!body.is_object())
        throw Error("请求内容必须为对象。", 422);
    for (auto it = body.begin(); it != body.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            throw Error("未知字段 " + it.key(), 422);
}
static void name(Json &body, const std::string &key, bool required) {
    if (!body.contains(key)) {
        if (required)
            throw Error("缺少字段 " + key, 422);
        return;
    }
    if (!body[key].is_string())
        throw Error(key + " 必须为非空文本。", 422);
    const std::string value = trim(body[key].get<std::string>());
    // Count UTF-8 leading bytes, so Chinese names and ASCII names share the limit.
    const auto count =
        std::count_if(value.begin(), value.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
    if (value.empty() || count > 200 || value.find('\0') != std::string::npos)
        throw Error(key + " 长度应为 1–200 个字符。", 422);
    body[key] = value;
}
int positive_int(const Json &body, const std::string &key, int maximum) {
    if (!body.contains(key) || !body[key].is_number_integer())
        throw Error(key + " 必须为整数。", 422);
    const auto value = body[key].get<std::int64_t>();
    if (value < 1 || value > maximum)
        throw Error(key + " 超出允许范围。", 422);
    return static_cast<int>(value);
}
void validate_station(Json &body, bool patch) {
    validate_keys(body, {"name_zh", "name_en", "district", "latitude", "longitude"});
    for (const auto &key : {"name_zh", "name_en", "district"})
        name(body, key, !patch);
    for (const auto &key : {"latitude", "longitude"}) {
        if (!body.contains(key)) {
            if (!patch)
                body[key] = nullptr;
            continue;
        }
        if (body[key].is_null())
            continue;
        const double bound = std::string(key) == "latitude" ? 90 : 180;
        if (!body[key].is_number() || !std::isfinite(body[key].get<double>()) ||
            std::abs(body[key].get<double>()) > bound)
            throw Error(std::string(key) + " 超出地理坐标范围。", 422);
    }
}
void validate_line(Json &body, bool patch) {
    validate_keys(body, {"name_zh", "name_en"});
    name(body, "name_zh", !patch);
    name(body, "name_en", !patch);
}
void validate_stop(Json &body, bool patch) {
    validate_keys(body, patch
                            ? std::vector<std::string>{"status", "opening_year", "first_train", "last_train"}
                            : std::vector<std::string>{"station_id", "status", "opening_year", "first_train",
                                                       "last_train"});
    if (!patch)
        positive_int(body, "station_id");
    if (!patch || body.contains("opening_year"))
        positive_int(body, "opening_year", 32767);
    if (!patch || body.contains("status")) {
        if (!body.contains("status") || !body["status"].is_string() ||
            (body["status"] != "Operation" && body["status"] != "Construction"))
            throw Error("status 应为 Operation 或 Construction。", 422);
    }
    const std::regex time_pattern(R"(^([01][0-9]|2[0-3]):[0-5][0-9](:[0-5][0-9])?$)");
    for (const auto &key : {"first_train", "last_train"}) {
        if (!body.contains(key)) {
            if (!patch)
                body[key] = nullptr;
            continue;
        }
        if (!body[key].is_null() &&
            (!body[key].is_string() || !std::regex_match(body[key].get<std::string>(), time_pattern)))
            throw Error(std::string(key) + " 应为 HH:MM 时间或 null。", 422);
    }
}
} // namespace metro
