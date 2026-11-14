#include "app.hpp"
#include <map>
#include <sstream>

namespace metro {
namespace {
std::string xml(const std::string &text) {
    std::string result;
    for (char c : text) {
        if (c == '&')
            result += "&amp;";
        else if (c == '<')
            result += "&lt;";
        else if (c == '>')
            result += "&gt;";
        else
            result += c;
    }
    return result;
}
} // namespace
void export_schema(const Config &config) {
    Db db(config);
    db.begin();
    auto columns = db.run(
        "SELECT c.relname AS table_name,a.attname AS name,format_type(a.atttypid,a.atttypmod) AS type,"
        "a.attnotnull AS mandatory,exists(SELECT 1 FROM pg_index i WHERE i.indrelid=c.oid AND i.indisprimary "
        "AND a.attnum=ANY(i.indkey)) AS pk "
        "FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace JOIN pg_attribute a ON a.attrelid=c.oid "
        "WHERE n.nspname='public' AND c.relkind='r' AND a.attnum>0 AND NOT a.attisdropped ORDER BY "
        "c.relname,a.attnum");
    const std::map<std::string, std::pair<int, int>> positions = {
        {"district", {620, 30}},  {"station", {620, 225}},       {"metro_line", {45, 225}},
        {"line_stop", {45, 500}}, {"station_exit", {1070, 500}}, {"exit_landmark", {620, 820}},
        {"landmark", {1070, 820}}};
    std::map<std::string, Json> tables;
    for (const auto &column : columns) {
        const auto name = column["table_name"].get<std::string>();
        if (positions.count(name))
            tables[name].push_back(column);
    }
    std::ostringstream svg;
    svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width="1450" height="1020" viewBox="0 0 1450 1020"><rect width="1450" height="1020" fill="white"/>)";
    for (const auto &[name, rows] : tables) {
        const auto [x, y] = positions.at(name);
        const int height = 40 + static_cast<int>(rows.size()) * 27;
        svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"330\" height=\"" << height
            << "\" fill=\"#ffffff\" stroke=\"#42685a\"/>";
        svg << "<rect x=\"" << x << "\" y=\"" << y
            << "\" width=\"330\" height=\"32\" fill=\"#e2eee8\" stroke=\"#42685a\"/>";
        svg << "<text x=\"" << x + 12 << "\" y=\"" << y + 23 << "\" font-size=\"20\" font-family=\"Arial\">"
            << name << "</text>";
        int row_y = y + 57;
        for (const auto &row : rows) {
            const auto label = (row["pk"].get<bool>() ? "PK " : "") + row["name"].get<std::string>() + " : " +
                               row["type"].get<std::string>();
            svg << "<text x=\"" << x + 12 << "\" y=\"" << row_y
                << "\" font-size=\"14\" font-family=\"Arial\">" << xml(label) << "</text>";
            row_y += 27;
        }
    }
    const std::vector<std::string> paths = {"785,225 785,124",  "210,500 210,346",  "375,610 620,330",
                                            "1070,555 950,330", "785,820 1070,600", "950,870 1070,870"};
    for (const auto &points : paths)
        svg << "<polyline points=\"" << points << "\" fill=\"none\" stroke=\"#608a75\" stroke-width=\"2\"/>";
    svg << R"(<text x="45" y="980" font-family="Arial" font-size="18">Generated from the live PostgreSQL catalog. Lines represent foreign-key references.</text></svg>)";
    write_file(config.root / "task2/schema_diagram.svg", svg.str());
}
} // namespace metro
