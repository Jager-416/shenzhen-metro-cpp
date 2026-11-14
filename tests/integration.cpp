#include "app.hpp"
#include "httplib.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace metro;
namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Action> void rejects(Action action, int status) {
    try {
        action();
    } catch (const Error &e) {
        check(e.status == status, "Unexpected error status: " + std::to_string(e.status));
        return;
    }
    throw std::runtime_error("Expected operation to fail");
}
// The supplied CSV fixtures contain no quoted comma fields. This independent
// reader is deliberately separate from the production CSV/SQL generator.
std::vector<std::map<std::string, std::string>> source_rows(const std::filesystem::path &path) {
    std::ifstream stream(path);
    std::string line;
    std::vector<std::vector<std::string>> rows;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        check(line.find('"') == std::string::npos, "Test fixture requires full CSV parser");
        if (rows.empty() && line.compare(0, 3, "\xef\xbb\xbf") == 0)
            line.erase(0, 3);
        std::stringstream parser(line);
        std::vector<std::string> cells;
        std::string cell;
        while (std::getline(parser, cell, ','))
            cells.push_back(cell);
        if (!line.empty() && line.back() == ',')
            cells.emplace_back();
        rows.push_back(cells);
    }
    std::vector<std::map<std::string, std::string>> result;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        check(rows[i].size() == rows[0].size(), "CSV width");
        std::map<std::string, std::string> record;
        for (std::size_t c = 0; c < rows[0].size(); ++c)
            record[rows[0][c]] = rows[i][c];
        result.push_back(record);
    }
    return result;
}
std::string with_database(const std::string &original, const std::string &name) {
    char *error = nullptr;
    PQconninfoOption *options = PQconninfoParse(original.c_str(), &error);
    if (!options) {
        std::string message = error ? error : "Invalid connection string";
        PQfreemem(error);
        throw std::runtime_error(message);
    }
    std::string result;
    for (auto *item = options; item->keyword; ++item) {
        if (!item->val || std::string(item->keyword) == "dbname")
            continue;
        result += std::string(item->keyword) + "='";
        for (char c : std::string(item->val)) {
            if (c == '\'' || c == '\\')
                result += '\\';
            result += c;
        }
        result += "' ";
    }
    PQconninfoFree(options);
    return result + "dbname=" + name;
}
Json new_station(Service &service, const std::string &name) {
    return service.save_station({{"name_zh", "测试" + name}, {"name_en", name}, {"district", "Nanshan"}});
}
Json stop(int id, const std::string &status = "Operation") {
    return {{"station_id", id}, {"status", status}, {"opening_year", 2026}};
}
} // namespace
int main(int, char **argv) {
    Config original;
    original.root = std::filesystem::absolute(argv[0]).parent_path().parent_path();
    const char *env = std::getenv("DATABASE_URL");
    original.database = env ? env : "host=127.0.0.1 port=55439 dbname=metro_project_cpp";
    const std::string name = "metro_cpp_test_" + std::to_string(getpid());
    Config config = original;
    config.database = with_database(original.database, name);
    config.port = 18081;
    pid_t child = -1;
    bool created = false;
    int passed = 0;
    try {
        {
            Db admin(original);
            admin.run("CREATE DATABASE " + name);
        }
        created = true;
        import_database(config);
        Service service(config);
        std::vector<std::pair<std::string, std::function<void()>>> cases;
        cases.emplace_back("repeatable import and generated identities", [&] {
            import_database(config);
            check(service.query(1)[0]["station_count"] == 439, "Station count");
            check(service.query(2)[0]["line_count"] == 29, "Line count");
            Db db(config);
            for (const auto &[table, count] : std::map<std::string, int>{{"district", 10},
                                                                         {"station", 439},
                                                                         {"metro_line", 29},
                                                                         {"line_stop", 567},
                                                                         {"station_exit", 1276},
                                                                         {"landmark", 2872},
                                                                         {"exit_landmark", 3203}})
                check(db.run("SELECT count(*) AS n FROM " + table)[0]["n"] == count, table);
            check(new_station(service, "Identity")["station_id"].get<int>() > 439, "Identity sequence");
        });
        cases.emplace_back("all 567 source station-line records", [&] {
            Db db(config);
            for (const auto &source : source_rows(config.root / "data/shenzhen_stations.csv")) {
                auto rows =
                    db.run("SELECT s.name_en,s.latitude,s.longitude,l.name_zh AS "
                           "line_zh,ls.position,ls.status,ls.opening_year,ls.first_train,ls.last_train "
                           "FROM station s JOIN district d USING(district_id) JOIN line_stop ls "
                           "USING(station_id) JOIN metro_line l USING(line_id) "
                           "WHERE s.name_zh=$1 AND d.name_en=$2 AND l.name_en=$3",
                           {source.at("Station Name"), source.at("District"), source.at("Line En")});
                check(rows.size() == 1, "Station identity match");
                const auto &row = rows[0];
                check(row["name_en"] == source.at("Station Name EN"), "English name");
                check(row["line_zh"] == source.at("Line Chineses"), "Line name");
                check(row["position"] == std::stoi(source.at("Num")), "Position");
                check(row["status"] == source.at("Status"), "Status");
                check(row["opening_year"] == std::stoi(source.at("Opening")), "Opening");
                for (const auto &[raw, key] : std::vector<std::pair<std::string, std::string>>{
                         {"Start Time", "first_train"}, {"End Time", "last_train"}})
                    check(source.at(raw).empty() ? row[key].is_null() : row[key] == source.at(raw) + ":00",
                          "Train time");
                if (!source.at("longitude").empty()) {
                    check(std::abs(row["latitude"].get<double>() - std::stod(source.at("longitude"))) < 1e-6,
                          "Latitude");
                    check(std::abs(row["longitude"].get<double>() - std::stod(source.at("latitude"))) < 1e-6,
                          "Longitude");
                }
            }
        });
        cases.emplace_back("all 1276 source exits and landmark sets", [&] {
            Db db(config);
            for (const auto &source : source_rows(config.root / "data/station_exits.csv")) {
                auto rows = db.run("SELECT e.exit_id,d.name_en AS district FROM station_exit e JOIN station "
                                   "s USING(station_id) "
                                   "JOIN district d USING(district_id) WHERE s.name_zh=$1 AND e.exit_code=$2",
                                   {source.at("station_name"), source.at("exit")});
                check(rows.size() == 1, "Exit identity");
                if (source.at("station_name") == "梨园")
                    check(rows[0]["district"] == "Longgang", "Liyuan mapping");
                std::set<std::string> expected, actual;
                std::string names = source.at("landmark");
                std::size_t start = 0;
                while (start <= names.size()) {
                    auto end = names.find("，", start);
                    auto value = trim(names.substr(start, end == std::string::npos ? end : end - start));
                    if (!value.empty())
                        expected.insert(value);
                    if (end == std::string::npos)
                        break;
                    start = end + 3;
                }
                for (const auto &row : db.run(
                         "SELECT name FROM exit_landmark JOIN landmark USING(landmark_id) WHERE exit_id=$1",
                         {text(rows[0]["exit_id"])}))
                    actual.insert(row["name"].get<std::string>());
                check(actual == expected, "Landmark set");
            }
        });
        cases.emplace_back("Q1 through Q7 and boundary cases", [&] {
            auto q3 = service.query(3);
            int sum = 0;
            for (const auto &row : q3)
                sum += row["station_count"].get<int>();
            check(sum == 350, "Operational count");
            check(service.query(4) == Json::array({{{"opening_year", 2011}, {"station_count", 27}},
                                                   {{"opening_year", 2019}, {"station_count", 7}},
                                                   {{"opening_year", 2025}, {"station_count", 3}}}),
                  "Q4");
            check(service.query(5).size() == 2, "Q5 ties");
            check(service.query(6).size() == 6, "Q6");
            auto q7 = service.query(7);
            check(q7[0]["get_station_n_neighbor"] == "Liwan" && q7[1]["get_station_n_neighbor"] == "Qianwan",
                  "Q7 example");
            auto bounds = service.query(7, {{"n", 5}});
            check(bounds[0]["get_station_n_neighbor"].is_null() &&
                      bounds[1]["get_station_n_neighbor"] == "Linhai",
                  "Q7 boundary");
            rejects([&] { service.query(7, {{"n", -1}}); }, 422);
            rejects([&] { service.query(7, {{"station_name", "missing"}}); }, 400);
            check(service.query(7, {{"n", 2147483647}})[1]["get_station_n_neighbor"].is_null(),
                  "Q7 overflow boundary");
        });
        cases.emplace_back("station CRUD and null coordinate validation", [&] {
            int id = new_station(service, "CRUD")["station_id"];
            service.save_station({{"name_en", "Updated"}, {"latitude", 22}, {"longitude", 114}}, id);
            check(service.station(id)["name_en"] == "Updated", "Station edit");
            rejects([&] { service.save_station({{"latitude", nullptr}}, id); }, 400);
            rejects([&] { service.save_station({{"name_en", nullptr}}, id); }, 422);
            rejects([&] { service.save_station({{"name_zh", " "}, {"name_en", "x"}, {"district", "x"}}); },
                    422);
            service.delete_station(id);
            rejects([&] { service.station(id); }, 404);
        });
        cases.emplace_back("line CRUD and ordered batch insertion", [&] {
            int line = service.save_line({{"name_zh", "测试线"}, {"name_en", "Test Line"}})["line_id"];
            service.save_line({{"name_en", "Changed"}}, line);
            int a = new_station(service, "A")["station_id"], b = new_station(service, "B")["station_id"],
                c = new_station(service, "C")["station_id"];
            service.insert_stops(line, {{"position", 1}, {"stops", Json::array({stop(a), stop(c)})}});
            service.insert_stops(line, {{"position", 2}, {"stops", Json::array({stop(b)})}});
            auto rows = service.line(line)["stops"];
            check(rows[0]["station_id"] == a && rows[1]["station_id"] == b && rows[2]["station_id"] == c,
                  "Insertion order");
            service.modify_stop(line, b, {{"status", "Construction"}});
            service.remove_stop(line, b);
            rows = service.line(line)["stops"];
            check(rows.size() == 2 && rows[1]["position"] == 2, "Compaction");
            service.delete_line(line);
            check(service.station(a)["lines"].empty(), "Keep physical station");
        });
        cases.emplace_back("invalid insertion is atomic", [&] {
            int id = new_station(service, "Rollback")["station_id"];
            auto before = service.line(1);
            rejects(
                [&] {
                    service.insert_stops(
                        1, {{"position", 2}, {"stops", Json::array({stop(id), stop(9999999)})}});
                },
                404);
            check(service.line(1) == before, "Batch rollback");
            rejects(
                [&] {
                    service.insert_stops(1, {{"position", 2}, {"stops", Json::array({stop(id), stop(id)})}});
                },
                400);
        });
        cases.emplace_back("deleting interchange cleans exits and compacts lines", [&] {
            int id = service.stations("老街")[0]["station_id"];
            auto detail = service.station(id);
            service.delete_station(id);
            for (const auto &line : detail["lines"]) {
                int i = 1;
                const auto rows = service.line(line["line_id"])["stops"];
                for (const auto &row : rows)
                    check(row["position"] == i++, "Interchange compaction");
            }
            Db db(config);
            check(db.run("SELECT count(*) AS n FROM station_exit WHERE station_id=$1",
                         {std::to_string(id)})[0]["n"] == 0,
                  "Deleted exits");
        });
        cases.emplace_back("concurrent insertions preserve contiguous positions", [&] {
            int line = service.save_line({{"name_zh", "并发线"}, {"name_en", "Concurrent"}})["line_id"];
            int a = new_station(service, "A")["station_id"], b = new_station(service, "B")["station_id"];
            auto insert = [&](int id) {
                service.insert_stops(line, {{"position", 1}, {"stops", Json::array({stop(id)})}});
            };
            auto first = std::async(std::launch::async, insert, a),
                 second = std::async(std::launch::async, insert, b);
            first.get();
            second.get();
            auto rows = service.line(line)["stops"];
            check(rows.size() == 2 && rows[0]["position"] == 1 && rows[1]["position"] == 2,
                  "Concurrent order");
        });
        cases.emplace_back("shortest path checked against independent BFS", [&] {
            int a = service.stations("Luohu")[0]["station_id"],
                b = service.stations("Shenzhen North Station")[0]["station_id"];
            std::map<int, std::set<int>> edges;
            for (const auto &line : service.lines()) {
                auto rows = service.line(line["line_id"])["stops"];
                for (std::size_t i = 1; i < rows.size(); ++i)
                    if (rows[i - 1]["status"] == "Operation" && rows[i]["status"] == "Operation") {
                        int left = rows[i - 1]["station_id"], right = rows[i]["station_id"];
                        edges[left].insert(right);
                        edges[right].insert(left);
                    }
            }
            std::queue<int> queue;
            queue.push(a);
            std::map<int, int> distance{{a, 0}};
            while (!queue.empty()) {
                int id = queue.front();
                queue.pop();
                for (int next : edges[id])
                    if (!distance.count(next)) {
                        distance[next] = distance[id] + 1;
                        queue.push(next);
                    }
            }
            check(service.route(a, b)["stops"] == distance.at(b), "Shortest path");
            check(service.route(a, a)["stops"] == 0, "Same station");
            check(service.stations("梨园").size() == 2, "Homonym separation");
        });
        cases.emplace_back("transfer preference and construction filtering", [&] {
            std::map<char, int> ids;
            for (char c : std::string("ABCDEFG"))
                ids[c] = new_station(service, std::string(1, c))["station_id"];
            int index = 0;
            for (const auto &path : {"ABCD", "AE", "ED", "AGD"}) {
                int line = service.save_line({{"name_zh", "路径" + std::to_string(index)},
                                              {"name_en", "Path" + std::to_string(index++)}})["line_id"];
                Json stops = Json::array();
                for (char c : std::string(path))
                    stops.push_back(stop(ids[c], c == 'G' ? "Construction" : "Operation"));
                service.insert_stops(line, {{"position", 1}, {"stops", stops}});
            }
            check(service.route(ids['A'], ids['D'])["stops"] == 2, "Stops preference");
            check(service.route(ids['A'], ids['D'], false, "transfers")["stops"] == 3, "Transfer preference");
            auto planned = service.route(ids['A'], ids['D'], true);
            check(planned["stops"] == 2 && planned["transfers"] == 0, "Construction filter");
        });
        cases.emplace_back("landmark routes include entrances", [&] {
            int a = service.landmarks("罗湖商业城")[0]["landmark_id"],
                b = service.landmarks("深圳市龙岗中心医院")[0]["landmark_id"];
            auto result = service.landmark_route(a, b);
            check(!result["origin_exits"].empty() && !result["destination_exits"].empty(), "Landmark exits");
        });
        cases.emplace_back("published fare coverage and distance boundaries", [&] {
            auto fares = Json::parse(read_file(config.root / "task4/data/fares.json"));
            check(fares["identities"].size() == 350, "Fare station count");
            for (std::size_t a = 0; a < 350; ++a)
                for (std::size_t b = 0; b < 350; ++b)
                    check(fares["fares"][a][b].is_number_integer() &&
                              fares["fares"][a][b] == fares["fares"][b][a],
                          "Fare symmetry");
            int a = service.stations("Luohu")[0]["station_id"],
                b = service.stations("Guomao")[0]["station_id"];
            check(service.fare(a, b)["kind"] == "published_table" && service.fare(a, b)["amount_cny"] == 2,
                  "Published fare");
            for (const auto &[km, expected] : std::vector<std::pair<double, int>>{{4, 2},
                                                                                  {4.001, 3},
                                                                                  {8, 3},
                                                                                  {12, 4},
                                                                                  {12.001, 5},
                                                                                  {18, 5},
                                                                                  {24, 6},
                                                                                  {24.001, 7},
                                                                                  {32, 7}})
                check(service.fare(a, b, km)["amount_cny"] == expected, "Fare boundary");
        });
        cases.emplace_back("parameter binding and input schema", [&] {
            check(service.stations("'; DROP TABLE station; --").empty(), "SQL injection search");
            rejects([&] { service.save_line({{"name_zh", "x"}, {"name_en", "y"}, {"unknown", true}}); }, 422);
            rejects(
                [&] {
                    service.save_station(
                        {{"name_zh", "x"}, {"name_en", "y"}, {"district", "Nanshan"}, {"latitude", 200}});
                },
                422);
            check(service.query(1)[0]["station_count"] == 439, "Tables preserved");
        });
        cases.emplace_back("real HTTP and static web entry", [&] {
            std::cout.flush();
            child = fork();
            check(child >= 0, "fork");
            if (child == 0) {
                try {
                    serve(config);
                    _exit(0);
                } catch (...) {
                    _exit(1);
                }
            }
            httplib::Client client("127.0.0.1", config.port);
            client.set_connection_timeout(1, 0);
            client.set_read_timeout(5, 0);
            bool ready = false;
            for (int i = 0; i < 50; ++i) {
                auto r = client.Get("/health");
                if (r && r->status == 200) {
                    ready = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            check(ready, "HTTP server ready");
            auto home = client.Get("/");
            check(home && home->status == 200 && home->body.find("/assets/app.js") != std::string::npos,
                  "HTML entry");
            check(client.Get("/assets/app.js")->status == 200, "JavaScript asset");
            check(Json::parse(client.Get("/api/queries/q1")->body)[0]["station_count"] == 439, "HTTP Q1");
            check(client.Post("/api/stations", "{}", "application/json")->status == 422, "HTTP validation");
            check(client.Get("/api/routes?destination=1")->status == 422, "Missing origin");
            auto created = client.Post("/api/lines", R"({"name_zh":"HTTP测试","name_en":"HTTP Test"})",
                                       "application/json");
            check(created && created->status == 201, "HTTP create");
            int id = Json::parse(created->body)["line_id"];
            check(client.Patch("/api/lines/" + std::to_string(id), R"({"name_en":"HTTP Updated"})",
                               "application/json")
                          ->status == 200,
                  "HTTP patch");
            check(client.Delete("/api/lines/" + std::to_string(id))->status == 200, "HTTP delete");
            kill(child, SIGTERM);
            waitpid(child, nullptr, 0);
            child = -1;
        });
        for (const auto &[label, test] : cases) {
            import_database(config);
            test();
            ++passed;
            std::cout << "PASS " << label << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
    }
    if (child > 0) {
        kill(child, SIGTERM);
        waitpid(child, nullptr, 0);
    }
    if (created) {
        try {
            Db admin(original);
            admin.run("DROP DATABASE " + name);
        } catch (const std::exception &error) {
            std::cerr << "Cleanup: " << error.what() << '\n';
        }
    }
    std::cout << passed << " / 15 integration checks passed\n";
    return passed == 15 ? 0 : 1;
}
