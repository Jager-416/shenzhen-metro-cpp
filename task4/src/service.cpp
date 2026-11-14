#include "app.hpp"
#include <set>

namespace metro {
static std::optional<std::string> parameter(const Json &value) {
    return value.is_null() ? std::nullopt : std::optional<std::string>(text(value));
}
Service::Service(Config config)
    : config_(std::move(config)), fares_(Json::parse(read_file(config_.root / "task4/data/fares.json"))) {}
Json Service::required(Db &db, const std::string &table, const std::string &key, int id) const {
    auto rows = db.run("SELECT * FROM " + table + " WHERE " + key + "=$1", {std::to_string(id)});
    if (rows.empty())
        throw Error(table + " " + std::to_string(id) + " not found", 404);
    return rows[0];
}
int Service::district_id(Db &db, const std::string &name) const {
    return db
        .run("INSERT INTO district(name_en) VALUES($1) ON CONFLICT(name_en) DO UPDATE "
             "SET name_en=excluded.name_en RETURNING district_id",
             {name})[0]["district_id"]
        .get<int>();
}
void Service::update(Db &db, const std::string &table, const std::string &key, int id,
                     const Json &values) const {
    if (values.empty())
        return;
    std::string statement = "UPDATE " + table + " SET ";
    Params parameters;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!parameters.empty())
            statement += ",";
        parameters.push_back(parameter(it.value()));
        statement += it.key() + "=$" + std::to_string(parameters.size());
    }
    parameters.push_back(std::to_string(id));
    statement += " WHERE " + key + "=$" + std::to_string(parameters.size());
    db.run(statement, parameters);
}
void Service::compact_line(Db &db, int id) const {
    db.run("SET CONSTRAINTS line_stop_position_key DEFERRED");
    db.run("WITH ranked AS (SELECT station_id,row_number() OVER(ORDER BY position) AS n "
           "FROM line_stop WHERE line_id=$1) UPDATE line_stop ls SET position=r.n FROM ranked r "
           "WHERE ls.line_id=$1 AND ls.station_id=r.station_id",
           {std::to_string(id)});
}
Json Service::health() const {
    Db db(config_);
    return {{"status", "ok"},
            {"backend", "C++17"},
            {"database", db.run("SELECT current_database() AS name")[0]["name"]},
            {"station_count", db.run("SELECT count(*) AS n FROM station")[0]["n"]}};
}
Json Service::districts() const {
    Db db(config_);
    return db.run("SELECT * FROM district ORDER BY name_en");
}
Json Service::stations(std::string search, std::optional<std::string> district, int limit, int offset) const {
    if (limit < 1 || limit > 2000 || offset < 0)
        throw Error("分页参数超出范围。", 422);
    Db db(config_);
    return db.run("SELECT s.*,d.name_en AS district FROM station s JOIN district d USING(district_id) "
                  "WHERE (s.name_zh ILIKE $1 OR s.name_en ILIKE $1) AND ($2::text IS NULL OR d.name_en=$2) "
                  "ORDER BY s.station_id LIMIT $3 OFFSET $4",
                  {"%" + search + "%", district, std::to_string(limit), std::to_string(offset)});
}
Json Service::station(int id) const {
    Db db(config_);
    db.begin();
    auto row = required(db, "station", "station_id", id);
    row["district"] = required(db, "district", "district_id", row["district_id"])["name_en"];
    row["lines"] =
        db.run("SELECT l.*,ls.position,ls.status,ls.opening_year,ls.first_train,ls.last_train "
               "FROM line_stop ls JOIN metro_line l USING(line_id) WHERE station_id=$1 ORDER BY line_id",
               {std::to_string(id)});
    row["exits"] =
        db.run("SELECT e.exit_id,e.exit_code,coalesce(jsonb_agg(lm.name ORDER BY lm.name) "
               "FILTER(WHERE lm.name IS NOT NULL),'[]'::jsonb) AS landmarks FROM station_exit e "
               "LEFT JOIN exit_landmark el USING(exit_id) LEFT JOIN landmark lm USING(landmark_id) "
               "WHERE e.station_id=$1 GROUP BY e.exit_id ORDER BY e.exit_code",
               {std::to_string(id)});
    return row;
}
Json Service::save_station(Json body, std::optional<int> id) const {
    validate_station(body, id.has_value());
    Db db(config_);
    db.begin(true);
    Json fields = body;
    Json current =
        id ? required(db, "station", "station_id", *id) : Json{{"latitude", nullptr}, {"longitude", nullptr}};
    if (fields.contains("district")) {
        fields["district_id"] = district_id(db, fields["district"]);
        fields.erase("district");
    }
    current.update(fields);
    if (current["latitude"].is_null() != current["longitude"].is_null())
        throw Error("Both coordinates must be present or both must be null");
    Json result;
    if (id) {
        update(db, "station", "station_id", *id, fields);
        result = required(db, "station", "station_id", *id);
    } else {
        result = db.run("INSERT INTO station(name_zh,name_en,district_id,latitude,longitude) "
                        "VALUES($1,$2,$3,$4,$5) RETURNING *",
                        {text(fields["name_zh"]), text(fields["name_en"]), text(fields["district_id"]),
                         parameter(fields["latitude"]), parameter(fields["longitude"])})[0];
    }
    db.commit();
    return result;
}
Json Service::delete_station(int id) const {
    Db db(config_);
    db.begin(true);
    required(db, "station", "station_id", id);
    const Params params{std::to_string(id)};
    const auto lines = db.run("SELECT line_id FROM line_stop WHERE station_id=$1", params);
    db.run(
        "DELETE FROM exit_landmark WHERE exit_id IN (SELECT exit_id FROM station_exit WHERE station_id=$1)",
        params);
    db.run("DELETE FROM station_exit WHERE station_id=$1", params);
    db.run("DELETE FROM line_stop WHERE station_id=$1", params);
    for (const auto &line : lines)
        compact_line(db, line["line_id"]);
    db.run("DELETE FROM station WHERE station_id=$1", params);
    db.commit();
    return {{"deleted_station_id", id}};
}
Json Service::lines(std::string search) const {
    Db db(config_);
    return db.run("SELECT l.*,count(ls.station_id) AS station_count FROM metro_line l "
                  "LEFT JOIN line_stop ls USING(line_id) WHERE l.name_zh ILIKE $1 OR l.name_en ILIKE $1 "
                  "GROUP BY l.line_id ORDER BY l.line_id",
                  {"%" + search + "%"});
}
Json Service::line(int id) const {
    Db db(config_);
    db.begin();
    auto row = required(db, "metro_line", "line_id", id);
    row["stops"] = db.run("SELECT ls.*,s.name_zh,s.name_en,d.name_en AS district FROM line_stop ls "
                          "JOIN station s USING(station_id) JOIN district d USING(district_id) WHERE "
                          "line_id=$1 ORDER BY position",
                          {std::to_string(id)});
    return row;
}
Json Service::save_line(Json body, std::optional<int> id) const {
    validate_line(body, id.has_value());
    Db db(config_);
    db.begin(true);
    Json result;
    if (id) {
        required(db, "metro_line", "line_id", *id);
        update(db, "metro_line", "line_id", *id, body);
        result = required(db, "metro_line", "line_id", *id);
    } else
        result = db.run("INSERT INTO metro_line(name_zh,name_en) VALUES($1,$2) RETURNING *",
                        {text(body["name_zh"]), text(body["name_en"])})[0];
    db.commit();
    return result;
}
Json Service::delete_line(int id) const {
    Db db(config_);
    db.begin(true);
    required(db, "metro_line", "line_id", id);
    db.run("DELETE FROM line_stop WHERE line_id=$1", {std::to_string(id)});
    db.run("DELETE FROM metro_line WHERE line_id=$1", {std::to_string(id)});
    db.commit();
    return {{"deleted_line_id", id}};
}
Json Service::insert_stops(int id, Json body) const {
    validate_keys(body, {"position", "stops"});
    const int position = positive_int(body, "position");
    if (!body.contains("stops") || !body["stops"].is_array() || body["stops"].empty() ||
        body["stops"].size() > 1000)
        throw Error("stops 应包含 1–1000 个站点。", 422);
    std::set<int> incoming;
    for (auto &stop : body["stops"]) {
        validate_stop(stop, false);
        if (!incoming.insert(stop["station_id"].get<int>()).second)
            throw Error("Batch contains duplicate station ids");
    }
    Db db(config_);
    db.begin(true);
    required(db, "metro_line", "line_id", id);
    const auto current = db.run("SELECT station_id FROM line_stop WHERE line_id=$1", {std::to_string(id)});
    if (position > static_cast<int>(current.size()) + 1)
        throw Error("插入位置超出线路长度。 ");
    for (const auto &row : current)
        if (incoming.count(row["station_id"]))
            throw Error("A station is already on this line", 409);
    for (int station_id : incoming)
        required(db, "station", "station_id", station_id);
    compact_line(db, id);
    db.run("UPDATE line_stop SET position=position+$1 WHERE line_id=$2 AND position >= $3",
           {std::to_string(incoming.size()), std::to_string(id), std::to_string(position)});
    int next = position;
    Json ids = Json::array();
    for (const auto &stop : body["stops"]) {
        ids.push_back(stop["station_id"]);
        db.run(
            "INSERT INTO line_stop(line_id,station_id,position,status,opening_year,first_train,last_train) "
            "VALUES($1,$2,$3,$4,$5,$6,$7)",
            {std::to_string(id), text(stop["station_id"]), std::to_string(next++), text(stop["status"]),
             text(stop["opening_year"]), parameter(stop["first_train"]), parameter(stop["last_train"])});
    }
    db.commit();
    return {{"line_id", id}, {"position", position}, {"inserted_station_ids", ids}};
}
Json Service::remove_stop(int line_id, int station_id) const {
    Db db(config_);
    db.begin(true);
    required(db, "metro_line", "line_id", line_id);
    auto rows = db.run("DELETE FROM line_stop WHERE line_id=$1 AND station_id=$2 RETURNING station_id",
                       {std::to_string(line_id), std::to_string(station_id)});
    if (rows.empty())
        throw Error("Station is not on this line", 404);
    compact_line(db, line_id);
    db.commit();
    return {{"line_id", line_id}, {"removed_station_id", station_id}};
}
Json Service::modify_stop(int line_id, int station_id, Json body) const {
    validate_stop(body, true);
    Db db(config_);
    db.begin(true);
    const Params keys{std::to_string(line_id), std::to_string(station_id)};
    if (db.run("SELECT 1 FROM line_stop WHERE line_id=$1 AND station_id=$2", keys).empty())
        throw Error("Station is not on this line", 404);
    if (!body.empty()) {
        std::string statement = "UPDATE line_stop SET ";
        Params parameters;
        for (auto it = body.begin(); it != body.end(); ++it) {
            if (!parameters.empty())
                statement += ",";
            parameters.push_back(parameter(it.value()));
            statement += it.key() + "=$" + std::to_string(parameters.size());
        }
        parameters.push_back(keys[0]);
        parameters.push_back(keys[1]);
        statement += " WHERE line_id=$" + std::to_string(parameters.size() - 1) + " AND station_id=$" +
                     std::to_string(parameters.size());
        db.run(statement, parameters);
    }
    auto result = db.run("SELECT * FROM line_stop WHERE line_id=$1 AND station_id=$2", keys)[0];
    db.commit();
    return result;
}
Json Service::query(int number, const Json &parameters) const {
    if (number < 1 || number > 7)
        throw Error("查询编号应为 1–7。", 422);
    Params values;
    if (number == 4 || number == 5)
        values = {parameters.value("line_name", number == 4 ? "Line 5" : "Line 1")};
    if (number == 6)
        values = {parameters.value("pattern", "塘朗%")};
    if (number == 7) {
        Json n = parameters.value("n", Json(2));
        if (!n.is_number_integer() || n.get<std::int64_t>() < 0 || n.get<std::int64_t>() > 2147483647)
            throw Error("n 应为非负整数。", 422);
        values = {parameters.value("station_name", "Mawan"), parameters.value("line_name", "Line 5"),
                  text(n)};
    }
    Db db(config_);
    return db.run(read_file(config_.root / "task3/queries" / ("q" + std::to_string(number) + ".sql")),
                  values);
}
Json Service::landmarks(std::string search, int limit, int offset) const {
    if (limit < 1 || limit > 3000 || offset < 0)
        throw Error("分页参数超出范围。", 422);
    Db db(config_);
    return db.run("SELECT * FROM landmark WHERE name ILIKE $1 ORDER BY name LIMIT $2 OFFSET $3",
                  {"%" + search + "%", std::to_string(limit), std::to_string(offset)});
}
} // namespace metro
