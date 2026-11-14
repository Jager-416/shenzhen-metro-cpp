#include "app.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace metro {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw Error("无法读取文件 " + path.string(), 500);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}
void write_file(const std::filesystem::path &path, const std::string &contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    if (!stream)
        throw Error("无法写入文件 " + path.string(), 500);
}
std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}
std::string text(const Json &value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}
Db::Db(const Config &config) {
    const char *keywords[] = {"dbname", "connect_timeout", nullptr};
    const char *values[] = {config.database.c_str(), "5", nullptr};
    connection_ = PQconnectdbParams(keywords, values, 1);
    if (!connection_ || PQstatus(connection_) != CONNECTION_OK) {
        if (connection_)
            PQfinish(connection_);
        connection_ = nullptr;
        throw Error("数据库连接失败，请检查 DATABASE_URL 和 PostgreSQL 服务。", 503);
    }
    if (PQsetClientEncoding(connection_, "UTF8") != 0) {
        PQfinish(connection_);
        connection_ = nullptr;
        throw Error("无法设置数据库 UTF-8 编码。", 503);
    }
    // Bootstrap notices are routine and would obscure CLI prompts.
    PQsetNoticeProcessor(connection_, [](void *, const char *) {}, nullptr);
}
Db::~Db() {
    if (connection_) {
        if (in_transaction_)
            PQclear(PQexec(connection_, "ROLLBACK"));
        PQfinish(connection_);
    }
}
Json Db::run(const std::string &statement, const Params &values) {
    std::vector<const char *> pointers;
    for (const auto &value : values)
        pointers.push_back(value ? value->c_str() : nullptr);
    PGresult *raw = values.empty()
                        ? PQexec(connection_, statement.c_str())
                        : PQexecParams(connection_, statement.c_str(), static_cast<int>(values.size()),
                                       nullptr, pointers.data(), nullptr, nullptr, 0);
    std::unique_ptr<PGresult, decltype(&PQclear)> result(raw, PQclear);
    if (!raw)
        throw Error("数据库查询失败。", 503);
    const auto status = PQresultStatus(raw);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        const char *sqlstate = PQresultErrorField(raw, PG_DIAG_SQLSTATE);
        const std::string code = sqlstate ? sqlstate : "";
        if (code == "23505")
            throw Error("Name, identity or line position already exists", 409);
        if (code == "23503")
            throw Error("关联记录不存在或仍被引用。", 409);
        const char *detail = PQresultErrorField(raw, PG_DIAG_MESSAGE_PRIMARY);
        if (code == "22023" || code == "23514" || code == "23502" || code.rfind("22", 0) == 0)
            throw Error(detail ? detail : "数据不符合约束。", 400);
        std::cerr << "SQL error [" << code << "] " << (detail ? detail : "unknown") << '\n';
        throw Error("数据库操作失败，请查看服务日志。", 503);
    }
    Json rows = Json::array();
    for (int r = 0; r < PQntuples(raw); ++r) {
        Json row = Json::object();
        for (int c = 0; c < PQnfields(raw); ++c) {
            const std::string name = PQfname(raw, c);
            if (PQgetisnull(raw, r, c)) {
                row[name] = nullptr;
                continue;
            }
            const std::string value = PQgetvalue(raw, r, c);
            switch (PQftype(raw, c)) {
            case 16:
                row[name] = value == "t";
                break;
            case 20:
            case 21:
            case 23:
                row[name] = std::stoll(value);
                break;
            case 700:
            case 701:
            case 1700:
                row[name] = std::stod(value);
                break;
            case 114:
            case 3802:
                row[name] = Json::parse(value);
                break;
            default:
                row[name] = value;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
void Db::begin(bool write) {
    run(write ? "BEGIN" : "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    in_transaction_ = true;
    run("SET LOCAL search_path TO public");
    if (write)
        run("SELECT pg_advisory_xact_lock(3072026)");
}
void Db::commit() {
    run("COMMIT");
    in_transaction_ = false;
}
void import_database(const Config &config, bool only_if_missing) {
    Db db(config);
    if (only_if_missing && !db.run("SELECT to_regclass('public.station') AS name")[0]["name"].is_null())
        return;
    db.run(read_file(config.root / "task3/import.sql"));
    std::cout << "数据库导入完成。\n";
}
} // namespace metro
