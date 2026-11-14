#include "app.hpp"
#include <charconv>
#include <iostream>

namespace metro {
namespace {
std::string ask(const std::string &label) {
    std::cout << label << " > " << std::flush;
    std::string value;
    if (!std::getline(std::cin, value))
        throw Error("输入已结束。", 499);
    return trim(value);
}
int number(const std::string &label, int minimum = 1) {
    const auto value = ask(label);
    int result;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc() || end != value.data() + value.size() || result < minimum)
        throw Error("请输入有效整数。");
    return result;
}
Json optional_number(const std::string &label) {
    const auto value = ask(label + "（回车留空）");
    if (value.empty())
        return nullptr;
    std::size_t end = 0;
    double result = std::stod(value, &end);
    if (end != value.size())
        throw Error("请输入有效数字。");
    return result;
}
Json station_body() {
    Json body;
    body["name_zh"] = ask("中文站名");
    body["name_en"] = ask("英文站名");
    body["district"] = ask("行政区英文名");
    body["latitude"] = optional_number("纬度");
    body["longitude"] = optional_number("经度");
    return body;
}
Json line_body() {
    Json body;
    body["name_zh"] = ask("线路中文名");
    body["name_en"] = ask("线路英文名");
    return body;
}
Json metadata() {
    Json body;
    body["status"] = ask("状态 Operation / Construction");
    body["opening_year"] = number("开通或预计开通年份");
    body["first_train"] = ask("首班 HH:MM（回车留空）");
    body["last_train"] = ask("末班 HH:MM（回车留空）");
    for (const auto &key : {"first_train", "last_train"})
        if (body[key] == "")
            body[key] = nullptr;
    return body;
}
Json patch() {
    std::cout << "输入修改字段 JSON，例如 {\"name_en\":\"New Name\"}。\n";
    return Json::parse(ask("修改内容"));
}
} // namespace
void cli(const Config &config) {
    Service service(config);
    while (true) {
        std::cout << "\n深圳地铁工作台（C++）\n"
                     "1 查找站点   2 站点详情   3 新增站点   4 修改站点   5 删除站点\n"
                     "6 查找线路   7 线路详情   8 新增线路   9 修改线路  10 删除线路\n"
                     "11 指定位置批量插站  12 从线路移除站点  13 Q1–Q7\n"
                     "14 两站路径  15 查找地标  16 两地标路线  17 票价  18 修改线路站点信息\n0 退出\n";
        try {
            const auto choice = ask("选择操作");
            Json result;
            if (choice == "0")
                return;
            if (choice == "1")
                result = service.stations(ask("站名关键词（回车列出全部）"));
            else if (choice == "2")
                result = service.station(number("station_id"));
            else if (choice == "3")
                result = service.save_station(station_body());
            else if (choice == "4") {
                int id = number("station_id");
                result = service.save_station(patch(), id);
            } else if (choice == "5") {
                int id = number("station_id");
                if (ask("确认删除？输入 yes") == "yes")
                    result = service.delete_station(id);
                else
                    continue;
            } else if (choice == "6")
                result = service.lines(ask("线路关键词（回车列出全部）"));
            else if (choice == "7")
                result = service.line(number("line_id"));
            else if (choice == "8")
                result = service.save_line(line_body());
            else if (choice == "9") {
                int id = number("line_id");
                result = service.save_line(patch(), id);
            } else if (choice == "10") {
                int id = number("line_id");
                if (ask("确认删除？输入 yes") == "yes")
                    result = service.delete_line(id);
                else
                    continue;
            } else if (choice == "11") {
                const int id = number("line_id"), position = number("插入位置"), count = number("插入数量");
                if (count > 1000)
                    throw Error("一次最多插入 1000 站。");
                Json stops = Json::array();
                for (int i = 0; i < count; ++i) {
                    std::cout << "第 " << i + 1 << " 站\n";
                    int sid = number("station_id");
                    auto stop = metadata();
                    stop["station_id"] = sid;
                    stops.push_back(stop);
                }
                result = service.insert_stops(id, {{"position", position}, {"stops", stops}});
            } else if (choice == "12") {
                int line = number("line_id"), station = number("station_id");
                result = service.remove_stop(line, station);
            } else if (choice == "13") {
                int n = number("查询编号 1–7");
                Json params = Json::object();
                if (n == 4 || n == 5 || n == 7)
                    params["line_name"] = ask("线路英文名");
                if (n == 6)
                    params["pattern"] = ask("地标 LIKE 条件，例如 塘朗%");
                if (n == 7) {
                    params["station_name"] = ask("站点英文名");
                    params["n"] = number("间隔站数 n", 0);
                }
                result = service.query(n, params);
            } else if (choice == "14" || choice == "16") {
                const int a = number(choice == "14" ? "起点 station_id" : "起点 landmark_id");
                const int b = number(choice == "14" ? "终点 station_id" : "终点 landmark_id");
                const bool construction = ask("包含在建区段？y/N") == "y";
                const auto preference = ask("少换乘优先？y/N") == "y" ? "transfers" : "stops";
                result = choice == "14" ? service.route(a, b, construction, preference)
                                        : service.landmark_route(a, b, construction, preference);
            } else if (choice == "15")
                result = service.landmarks(ask("地标关键词"));
            else if (choice == "17") {
                int a = number("起点 station_id"), b = number("终点 station_id");
                result = service.fare(a, b);
            } else if (choice == "18") {
                int line = number("line_id"), station = number("station_id");
                result = service.modify_stop(line, station, metadata());
            } else {
                std::cout << "请选择菜单内的编号。\n";
                continue;
            }
            std::cout << result.dump(2) << '\n';
        } catch (const Error &error) {
            if (error.status == 499)
                return;
            std::cout << "操作失败：" << error.what() << '\n';
        } catch (const std::exception &error) {
            std::cout << "输入或操作失败：" << error.what() << '\n';
        }
    }
}
} // namespace metro
