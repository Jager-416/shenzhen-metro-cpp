#include "app.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace metro {
namespace {
using Record = std::map<std::string, std::string>;
std::vector<Record> csv(const std::filesystem::path &path) {
    std::string contents = read_file(path);
    if (contents.compare(0, 3, "\xef\xbb\xbf") == 0)
        contents.erase(0, 3);
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < contents.size(); ++i) {
        const char c = contents[i];
        if (quoted) {
            if (c == '"' && i + 1 < contents.size() && contents[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"')
                quoted = false;
            else
                field += c;
        } else if (c == '"' && field.empty())
            quoted = true;
        else if (c == ',') {
            row.push_back(field);
            field.clear();
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < contents.size() && contents[i + 1] == '\n')
                ++i;
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
        } else
            field += c;
    }
    if (quoted)
        throw Error("CSV 中存在未闭合的引号。 ");
    if (!row.empty() || !field.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    if (rows.empty())
        throw Error("CSV 文件为空。 ");
    std::vector<Record> records;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].size() != rows[0].size())
            throw Error("CSV 字段数量不一致，第 " + std::to_string(i + 1) + " 行。");
        Record record;
        for (std::size_t c = 0; c < rows[0].size(); ++c)
            record[rows[0][c]] = rows[i][c];
        records.push_back(std::move(record));
    }
    return records;
}
std::string quote(const Json &value) {
    if (value.is_null())
        return "NULL";
    if (value.is_number())
        return value.dump();
    std::string result = "'";
    for (char c : value.get<std::string>()) {
        result += c;
        if (c == '\'')
            result += '\'';
    }
    return result + "'";
}
std::string insert(const std::string &table, const std::string &columns, const Json &rows,
                   bool identity = true) {
    if (rows.empty())
        return "";
    std::string output = "INSERT INTO " + table + " (" + columns + ") " +
                         (identity ? "OVERRIDING SYSTEM VALUE " : "") + "VALUES\n";
    for (std::size_t r = 0; r < rows.size(); ++r) {
        output += "(";
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            if (c)
                output += ",";
            output += quote(rows[r][c]);
        }
        output += r + 1 == rows.size() ? ");\n" : "),\n";
    }
    return output;
}
Json optional_text(const std::string &value) {
    return value.empty() ? Json(nullptr) : Json(value);
}
} // namespace
void build_import(const Config &config) {
    auto stations = csv(config.root / "data/shenzhen_stations.csv");
    auto exits = csv(config.root / "data/station_exits.csv");
    using Identity = std::pair<std::string, std::string>;
    std::map<std::string, int> districts, lines;
    std::map<std::string, std::string> line_chinese;
    std::map<Identity, std::vector<Record>> grouped;
    Json line_rows = Json::array();
    for (const auto &row : stations) {
        districts[row.at("District")] = 0;
        const auto en = row.at("Line En"), zh = row.at("Line Chineses");
        if (!lines.count(en)) {
            lines[en] = static_cast<int>(lines.size()) + 1;
            line_chinese[en] = zh;
            line_rows.push_back({lines[en], zh, en});
        }
        if (line_chinese.at(en) != zh)
            throw Error("线路中英文名不一致。 ");
        grouped[{row.at("Station Name"), row.at("District")}].push_back(row);
    }
    Json district_rows = Json::array(), station_rows = Json::array(), stop_rows = Json::array();
    int next = 1;
    for (auto &[name, id] : districts) {
        id = next++;
        district_rows.push_back({id, name});
    }
    std::map<Identity, int> station_ids;
    std::map<std::string, std::vector<Identity>> candidates;
    for (const auto &[key, rows] : grouped) {
        const int id = static_cast<int>(station_ids.size()) + 1;
        station_ids[key] = id;
        candidates[key.first].push_back(key);
        auto one = [&](const std::string &field) {
            std::set<std::string> values;
            for (const auto &row : rows)
                if (!row.at(field).empty())
                    values.insert(row.at(field));
            if (values.size() > 1)
                throw Error("同站字段存在冲突 " + key.first + " / " + field);
            return values.empty() ? std::string() : *values.begin();
        };
        const auto lat = one("longitude"), lon = one("latitude"); // Official headers are reversed.
        station_rows.push_back({id, districts.at(key.second), key.first, one("Station Name EN"),
                                lat.empty() ? Json(nullptr) : Json(std::stod(lat)),
                                lon.empty() ? Json(nullptr) : Json(std::stod(lon))});
    }
    for (const auto &row : stations)
        stop_rows.push_back({lines.at(row.at("Line En")),
                             station_ids.at({row.at("Station Name"), row.at("District")}),
                             std::stoi(row.at("Num")), row.at("Status"), std::stoi(row.at("Opening")),
                             optional_text(row.at("Start Time")), optional_text(row.at("End Time"))});
    Json exit_rows = Json::array(), landmark_rows = Json::array(), links = Json::array();
    std::map<std::string, int> landmark_ids;
    int exit_id = 0;
    for (const auto &row : exits) {
        ++exit_id;
        const auto &options = candidates.at(row.at("station_name"));
        Identity key;
        if (options.size() == 1)
            key = options[0];
        else if (row.at("station_name") == "梨园")
            key = {"梨园", "Longgang"};
        else
            throw Error("出口站名存在歧义。 ");
        if (row.at("station_name_en") != grouped.at(key)[0].at("Station Name EN"))
            throw Error("出口英文站名不匹配。 ");
        exit_rows.push_back({exit_id, station_ids.at(key), row.at("exit")});
        const auto &names = row.at("landmark");
        std::size_t start = 0;
        std::set<std::string> seen;
        while (start <= names.size()) {
            const auto separator = names.find("，", start);
            const auto name = trim(
                names.substr(start, separator == std::string::npos ? std::string::npos : separator - start));
            if (!name.empty() && seen.insert(name).second) {
                if (!landmark_ids.count(name)) {
                    landmark_ids[name] = static_cast<int>(landmark_ids.size()) + 1;
                    landmark_rows.push_back({landmark_ids.at(name), name});
                }
                links.push_back({exit_id, landmark_ids.at(name)});
            }
            if (separator == std::string::npos)
                break;
            start = separator + std::string("，").size();
        }
    }
    std::string output =
        "-- Generated by C++ build-import from the official CSV files.\nBEGIN;\n"
        "SELECT pg_advisory_xact_lock(3072026);\nSET LOCAL search_path TO public;\n"
        "SET LOCAL standard_conforming_strings=on;\n"
        "DROP FUNCTION IF EXISTS get_station_n_neighbor(text,text,integer);\n"
        "DROP TABLE IF EXISTS exit_landmark,station_exit,landmark,line_stop,station,metro_line,district;\n";
    output += read_file(config.root / "task2/schema.sql") + "\n";
    output += insert("district", "district_id,name_en", district_rows);
    output += insert("metro_line", "line_id,name_zh,name_en", line_rows);
    output += insert("station", "station_id,district_id,name_zh,name_en,latitude,longitude", station_rows);
    output += insert("line_stop", "line_id,station_id,position,status,opening_year,first_train,last_train",
                     stop_rows, false);
    output += insert("station_exit", "exit_id,station_id,exit_code", exit_rows);
    output += insert("landmark", "landmark_id,name", landmark_rows);
    output += insert("exit_landmark", "exit_id,landmark_id", links, false);
    for (const auto &[table, column] :
         std::vector<std::pair<std::string, std::string>>{{"district", "district_id"},
                                                          {"metro_line", "line_id"},
                                                          {"station", "station_id"},
                                                          {"station_exit", "exit_id"},
                                                          {"landmark", "landmark_id"}})
        output += "SELECT setval(pg_get_serial_sequence('" + table + "','" + column + "'),(SELECT max(" +
                  column + ") FROM " + table + "),true);\n";
    output += "CREATE INDEX line_stop_station_idx ON line_stop(station_id);\n"
              "CREATE INDEX exit_landmark_landmark_idx ON exit_landmark(landmark_id);\n"
              "CREATE INDEX station_name_en_idx ON station(name_en);\n"
              "CREATE INDEX landmark_prefix_idx ON landmark(name text_pattern_ops);\n";
    output += read_file(config.root / "task3/functions.sql") + "\nCOMMIT;\n";
    write_file(config.root / "task3/import.sql", output);
    Json counts = {{"district", district_rows.size()}, {"metro_line", line_rows.size()},
                   {"station", station_rows.size()},   {"line_stop", stop_rows.size()},
                   {"station_exit", exit_rows.size()}, {"landmark", landmark_rows.size()},
                   {"exit_landmark", links.size()}};
    write_file(config.root / "task3/import_manifest.json", counts.dump(2) + "\n");
    std::cout << "已生成 task3/import.sql\n" << counts.dump(2) << '\n';
}
} // namespace metro
