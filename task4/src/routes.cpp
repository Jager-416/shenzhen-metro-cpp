#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <tuple>

namespace metro {
namespace {
struct Network {
    std::map<int, Json> stations;
    std::map<int, std::vector<std::pair<int, int>>> edges;
    std::map<int, Json> lines;
};
Network load_network(Db &db, bool construction) {
    Network network;
    for (const auto &row :
         db.run("SELECT s.*,d.name_en AS district FROM station s JOIN district d USING(district_id)"))
        network.stations[row["station_id"].get<int>()] = row;
    const auto stops =
        db.run("SELECT ls.*,l.name_en,l.name_zh FROM line_stop ls JOIN metro_line l USING(line_id) "
               "ORDER BY ls.line_id,ls.position");
    for (const auto &row : stops)
        network.lines[row["line_id"].get<int>()] = {{"name_en", row["name_en"]}, {"name_zh", row["name_zh"]}};
    for (std::size_t i = 1; i < stops.size(); ++i) {
        const auto &left = stops[i - 1];
        const auto &right = stops[i];
        if (left["line_id"] != right["line_id"])
            continue;
        // Filter after forming adjacent pairs: never jump over a construction stop.
        if (!construction && (left["status"] != "Operation" || right["status"] != "Operation"))
            continue;
        const int a = left["station_id"], b = right["station_id"], line = left["line_id"];
        network.edges[a].emplace_back(b, line);
        network.edges[b].emplace_back(a, line);
    }
    return network;
}
Json shortest(const Network &network, const std::set<int> &origins, const std::set<int> &destinations,
              const std::string &preference) {
    if (preference != "stops" && preference != "transfers")
        throw Error("路线偏好应为 stops 或 transfers。", 422);
    using State = std::pair<int, int>; // station id, current line id (0 before boarding)
    using Score = std::pair<int, int>;
    using Entry = std::tuple<int, int, int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    std::map<State, Score> best;
    std::map<State, std::optional<State>> previous;
    for (int id : origins) {
        State state{id, 0};
        best[state] = {0, 0};
        previous[state] = std::nullopt;
        queue.emplace(0, 0, id, 0);
    }
    std::optional<State> found;
    while (!queue.empty()) {
        const auto [first, second, station, line] = queue.top();
        queue.pop();
        const State state{station, line};
        const Score score{first, second};
        if (best.at(state) != score)
            continue;
        if (destinations.count(station)) {
            found = state;
            break;
        }
        auto adjacency = network.edges.find(station);
        if (adjacency == network.edges.end())
            continue;
        for (const auto &[next, new_line] : adjacency->second) {
            const int transfer = line && line != new_line ? 1 : 0;
            const Score next_score = preference == "stops" ? Score{first + 1, second + transfer}
                                                           : Score{first + transfer, second + 1};
            const State next_state{next, new_line};
            if (!best.count(next_state) || next_score < best.at(next_state)) {
                best[next_state] = next_score;
                previous[next_state] = state;
                queue.emplace(next_score.first, next_score.second, next, new_line);
            }
        }
    }
    if (!found)
        throw Error("No connected route under the selected operating-status filter", 404);
    std::vector<State> path;
    while (found) {
        path.push_back(*found);
        found = previous.at(*found);
    }
    std::reverse(path.begin(), path.end());
    Json stations = Json::array(), segments = Json::array();
    for (const auto &state : path)
        stations.push_back(network.stations.at(state.first));
    for (std::size_t i = 1; i < path.size(); ++i) {
        const int line = path[i].second;
        if (segments.empty() || segments.back()["line_id"] != line) {
            Json segment = network.lines.at(line);
            segment["line_id"] = line;
            segment["station_ids"] = Json::array({path[i - 1].first});
            segments.push_back(segment);
        }
        segments.back()["station_ids"].push_back(path[i].first);
    }
    return {{"origin_station_id", path.front().first},
            {"destination_station_id", path.back().first},
            {"stops", path.size() - 1},
            {"transfers", segments.empty() ? 0 : static_cast<int>(segments.size()) - 1},
            {"preference", preference},
            {"stations", stations},
            {"segments", segments}};
}
int distance_fare(double km) {
    if (km <= 4)
        return 2;
    if (km <= 12)
        return 2 + static_cast<int>(std::ceil((km - 4) / 4));
    if (km <= 24)
        return 4 + static_cast<int>(std::ceil((km - 12) / 6));
    return 6 + static_cast<int>(std::ceil((km - 24) / 8));
}
double haversine(const Json &a, const Json &b) {
    constexpr double rad = 3.14159265358979323846 / 180;
    const double lat1 = a["latitude"].get<double>() * rad, lat2 = b["latitude"].get<double>() * rad;
    const double dlat = lat2 - lat1,
                 dlon = (b["longitude"].get<double>() - a["longitude"].get<double>()) * rad;
    const double value =
        std::pow(std::sin(dlat / 2), 2) + std::cos(lat1) * std::cos(lat2) * std::pow(std::sin(dlon / 2), 2);
    return 6371.0088 * 2 * std::asin(std::min(1.0, std::sqrt(value)));
}
} // namespace
Json Service::route(int origin, int destination, bool construction, std::string preference) const {
    Db db(config_);
    db.begin();
    required(db, "station", "station_id", origin);
    required(db, "station", "station_id", destination);
    auto result = shortest(load_network(db, construction), {origin}, {destination}, preference);
    result["includes_construction"] = construction;
    return result;
}
Json Service::landmark_route(int origin, int destination, bool construction, std::string preference) const {
    Db db(config_);
    db.begin();
    required(db, "landmark", "landmark_id", origin);
    required(db, "landmark", "landmark_id", destination);
    auto entrances = [&](int id) {
        return db.run(
            "SELECT e.*,s.name_zh,s.name_en FROM exit_landmark el JOIN station_exit e USING(exit_id) "
            "JOIN station s USING(station_id) WHERE landmark_id=$1 ORDER BY e.station_id,e.exit_code",
            {std::to_string(id)});
    };
    const auto origins = entrances(origin), destinations = entrances(destination);
    if (origins.empty() || destinations.empty())
        throw Error("Landmark has no associated exits", 404);
    std::set<int> starts, ends;
    for (const auto &row : origins)
        starts.insert(row["station_id"].get<int>());
    for (const auto &row : destinations)
        ends.insert(row["station_id"].get<int>());
    auto result = shortest(load_network(db, construction), starts, ends, preference);
    result["origin_exits"] = Json::array();
    result["destination_exits"] = Json::array();
    for (const auto &row : origins)
        if (row["station_id"] == result["origin_station_id"])
            result["origin_exits"].push_back(row);
    for (const auto &row : destinations)
        if (row["station_id"] == result["destination_station_id"])
            result["destination_exits"].push_back(row);
    result["includes_construction"] = construction;
    return result;
}
Json Service::fare(int origin, int destination, std::optional<double> distance) const {
    const auto result = route(origin, destination);
    const std::string rule = "https://fgw.sz.gov.cn/gkmlpt/content/5/5737/post_5737294.html";
    if (distance) {
        if (!std::isfinite(*distance) || *distance <= 0 || *distance > 1000)
            throw Error("计费里程应大于 0 且不超过 1000 公里。", 422);
        return {{"amount_cny", distance_fare(*distance)},
                {"kind", "user_supplied_distance"},
                {"distance_km", *distance},
                {"source", rule},
                {"note", "按提供的计费里程计算普通车厢全价。"}};
    }
    const auto &stations = result["stations"];
    int a = -1, b = -1;
    for (std::size_t i = 0; i < fares_["identities"].size(); ++i) {
        const auto &identity = fares_["identities"][i];
        if (identity[0] == stations.front()["name_zh"] && identity[1] == stations.front()["district"])
            a = static_cast<int>(i);
        if (identity[0] == stations.back()["name_zh"] && identity[1] == stations.back()["district"])
            b = static_cast<int>(i);
    }
    if (a >= 0 && b >= 0 && !fares_["fares"][a][b].is_null())
        return {{"amount_cny", fares_["fares"][a][b]},
                {"kind", "published_table"},
                {"source", fares_["source"]},
                {"publication_date", fares_["publication_date"]},
                {"origin_station_id", origin},
                {"destination_station_id", destination},
                {"note", "2026 年普通车厢公布票价，使用来源线网快照。"}};
    for (const auto &row : stations)
        if (row["latitude"].is_null() || row["longitude"].is_null())
            return {{"amount_cny", nullptr},
                    {"kind", "missing_distance"},
                    {"source", rule},
                    {"route", result},
                    {"note", "缺少计费里程和沿途坐标，请提供计费里程后查询。"}};
    double km = 0;
    for (std::size_t i = 1; i < stations.size(); ++i)
        km += haversine(stations[i - 1], stations[i]);
    return {{"amount_cny", distance_fare(km)},
            {"kind", "coordinate_estimate"},
            {"distance_km", std::round(km * 1000) / 1000},
            {"source", rule},
            {"route", result},
            {"note", "按相邻站点坐标直线距离之和估算，供参考。"}};
}
} // namespace metro
