#pragma once
#include "json.hpp"
#include "postgresql/libpq-fe.h"
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace metro {
using Json = nlohmann::json;
using Params = std::vector<std::optional<std::string>>;

struct Error : std::runtime_error {
    int status;
    Error(std::string message, int code = 400) : std::runtime_error(std::move(message)), status(code) {}
};
struct Config {
    std::filesystem::path root;
    std::string database;
    std::string host = "127.0.0.1";
    int port = 8001;
};
std::string read_file(const std::filesystem::path &path);
void write_file(const std::filesystem::path &path, const std::string &contents);
std::string trim(std::string text);
std::string text(const Json &value);

// One connection and one transaction per operation. Destruction rolls back an
// unfinished transaction, so failures cannot commit half a batch.
class Db {
    PGconn *connection_ = nullptr;
    bool in_transaction_ = false;

  public:
    explicit Db(const Config &config);
    ~Db();
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;
    Json run(const std::string &statement, const Params &values = {});
    void begin(bool write = false);
    void commit();
};

class Service {
    Config config_;
    Json fares_;
    Json required(Db &db, const std::string &table, const std::string &key, int id) const;
    int district_id(Db &db, const std::string &name) const;
    void compact_line(Db &db, int line_id) const;
    void update(Db &db, const std::string &table, const std::string &key, int id, const Json &values) const;

  public:
    explicit Service(Config config);
    const Config &config() const {
        return config_;
    }
    Json health() const;
    Json districts() const;
    Json stations(std::string search = "", std::optional<std::string> district = {}, int limit = 1000,
                  int offset = 0) const;
    Json station(int id) const;
    Json save_station(Json body, std::optional<int> id = {}) const;
    Json delete_station(int id) const;
    Json lines(std::string search = "") const;
    Json line(int id) const;
    Json save_line(Json body, std::optional<int> id = {}) const;
    Json delete_line(int id) const;
    Json insert_stops(int line_id, Json body) const;
    Json modify_stop(int line_id, int station_id, Json body) const;
    Json remove_stop(int line_id, int station_id) const;
    Json query(int number, const Json &parameters = Json::object()) const;
    Json landmarks(std::string search = "", int limit = 100, int offset = 0) const;
    Json route(int origin, int destination, bool construction = false,
               std::string preference = "stops") const;
    Json landmark_route(int origin, int destination, bool construction = false,
                        std::string preference = "stops") const;
    Json fare(int origin, int destination, std::optional<double> distance = {}) const;
};
void validate_station(Json &body, bool patch);
void validate_line(Json &body, bool patch);
void validate_stop(Json &body, bool patch);
void validate_keys(const Json &body, const std::vector<std::string> &allowed);
int positive_int(const Json &body, const std::string &key, int maximum = 2147483647);
void build_import(const Config &config);
void export_schema(const Config &config);
void import_database(const Config &config, bool only_if_missing = false);
void serve(const Config &config);
void cli(const Config &config);
} // namespace metro
