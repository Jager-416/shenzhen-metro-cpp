#include "app.hpp"
#include "httplib.h"
#include <charconv>
#include <functional>
#include <iostream>

namespace metro {
namespace {
int integer(const std::string &value, int minimum = 0, int maximum = 2147483647) {
    int result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc() || end != value.data() + value.size() || result < minimum || result > maximum)
        throw Error("整数参数超出范围。", 422);
    return result;
}
std::string param(const httplib::Request &request, const std::string &key, const std::string &fallback = "") {
    return request.has_param(key) ? request.get_param_value(key) : fallback;
}
int int_param(const httplib::Request &request, const std::string &key, int fallback, int minimum = 0,
              int maximum = 2147483647) {
    if (request.has_param(key))
        return integer(request.get_param_value(key), minimum, maximum);
    if (fallback < minimum || fallback > maximum)
        throw Error("缺少参数 " + key, 422);
    return fallback;
}
bool construction(const httplib::Request &request) {
    const auto value = param(request, "include_construction", "false");
    if (value == "true" || value == "1")
        return true;
    if (value == "false" || value == "0")
        return false;
    throw Error("include_construction 应为 true 或 false。", 422);
}
Json body(const httplib::Request &request) {
    try {
        return Json::parse(request.body);
    } catch (const Json::exception &) {
        throw Error("请求 JSON 格式不正确。", 422);
    }
}
void respond(httplib::Response &response, const Json &value, int status = 200) {
    response.status = status;
    response.set_content(value.dump(), "application/json; charset=utf-8");
}
using Action = std::function<Json(const httplib::Request &)>;
httplib::Server::Handler handler(Action action, int status = 200) {
    return [action = std::move(action), status](const auto &request, auto &response) {
        try {
            respond(response, action(request), status);
        } catch (const Error &error) {
            respond(response, {{"detail", error.what()}}, error.status);
        } catch (const Json::exception &) {
            respond(response, {{"detail", "字段类型不符合要求。"}}, 422);
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            respond(response, {{"detail", "服务处理失败，请查看日志。"}}, 500);
        }
    };
}
} // namespace
void serve(const Config &config) {
    Service service(config);
    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(12); };
    server.set_payload_max_length(1024 * 1024);
    server.set_read_timeout(15, 0);
    server.set_write_timeout(15, 0);
    server.Get("/health", handler([&](const auto &) { return service.health(); }));
    server.Get("/api/districts", handler([&](const auto &) { return service.districts(); }));
    server.Get("/api/stations", handler([&](const auto &r) {
                   const auto district = r.has_param("district")
                                             ? std::optional<std::string>(r.get_param_value("district"))
                                             : std::nullopt;
                   return service.stations(param(r, "search"), district, int_param(r, "limit", 1000, 1, 2000),
                                           int_param(r, "offset", 0));
               }));
    server.Post("/api/stations", handler([&](const auto &r) { return service.save_station(body(r)); }, 201));
    server.Get(R"(/api/stations/([0-9]+))",
               handler([&](const auto &r) { return service.station(integer(r.matches[1], 1)); }));
    server.Patch(R"(/api/stations/([0-9]+))", handler([&](const auto &r) {
                     return service.save_station(body(r), integer(r.matches[1], 1));
                 }));
    server.Delete(R"(/api/stations/([0-9]+))",
                  handler([&](const auto &r) { return service.delete_station(integer(r.matches[1], 1)); }));
    server.Get("/api/lines", handler([&](const auto &r) { return service.lines(param(r, "search")); }));
    server.Post("/api/lines", handler([&](const auto &r) { return service.save_line(body(r)); }, 201));
    server.Get(R"(/api/lines/([0-9]+))",
               handler([&](const auto &r) { return service.line(integer(r.matches[1], 1)); }));
    server.Patch(R"(/api/lines/([0-9]+))", handler([&](const auto &r) {
                     return service.save_line(body(r), integer(r.matches[1], 1));
                 }));
    server.Delete(R"(/api/lines/([0-9]+))",
                  handler([&](const auto &r) { return service.delete_line(integer(r.matches[1], 1)); }));
    server.Post(
        R"(/api/lines/([0-9]+)/stops)",
        handler([&](const auto &r) { return service.insert_stops(integer(r.matches[1], 1), body(r)); }, 201));
    server.Patch(R"(/api/lines/([0-9]+)/stops/([0-9]+))", handler([&](const auto &r) {
                     return service.modify_stop(integer(r.matches[1], 1), integer(r.matches[2], 1), body(r));
                 }));
    server.Delete(R"(/api/lines/([0-9]+)/stops/([0-9]+))", handler([&](const auto &r) {
                      return service.remove_stop(integer(r.matches[1], 1), integer(r.matches[2], 1));
                  }));
    server.Get(R"(/api/queries/q([1-7]))", handler([&](const auto &r) {
                   Json parameters = Json::object();
                   for (const auto &key : {"line_name", "station_name", "pattern"})
                       if (r.has_param(key))
                           parameters[key] = r.get_param_value(key);
                   if (r.has_param("n"))
                       parameters["n"] = int_param(r, "n", 2);
                   return service.query(integer(r.matches[1], 1, 7), parameters);
               }));
    server.Get("/api/routes", handler([&](const auto &r) {
                   return service.route(int_param(r, "origin", 0, 1), int_param(r, "destination", 0, 1),
                                        construction(r), param(r, "preference", "stops"));
               }));
    server.Get("/api/landmarks", handler([&](const auto &r) {
                   return service.landmarks(param(r, "search"), int_param(r, "limit", 100, 1, 3000),
                                            int_param(r, "offset", 0));
               }));
    server.Get("/api/landmark-routes", handler([&](const auto &r) {
                   return service.landmark_route(int_param(r, "origin", 0, 1),
                                                 int_param(r, "destination", 0, 1), construction(r),
                                                 param(r, "preference", "stops"));
               }));
    server.Get("/api/fares", handler([&](const auto &r) {
                   std::optional<double> distance;
                   if (r.has_param("distance_km")) {
                       try {
                           auto value = r.get_param_value("distance_km");
                           std::size_t end = 0;
                           distance = std::stod(value, &end);
                           if (end != value.size())
                               throw std::invalid_argument("trailing characters");
                       } catch (const std::exception &) {
                           throw Error("计费里程必须是数字。", 422);
                       }
                   }
                   return service.fare(int_param(r, "origin", 0, 1), int_param(r, "destination", 0, 1),
                                       distance);
               }));
    if (!server.set_mount_point("/assets", (config.root / "task4/web").string()))
        throw Error("网页静态目录不存在。", 500);
    server.Get("/", [&](const auto &, auto &response) {
        response.set_content(read_file(config.root / "task4/web/index.html"), "text/html; charset=utf-8");
    });
    server.set_error_handler([](const auto &, auto &response) {
        if (response.body.empty())
            respond(response, {{"detail", "请求地址不存在或请求格式不正确。"}}, response.status);
    });
    std::cout << "C++ 地铁工作台 http://" << config.host << ':' << config.port << "/\n" << std::flush;
    if (!server.listen(config.host, config.port))
        throw Error("HTTP 端口监听失败，检查端口是否已占用。", 500);
}
} // namespace metro
