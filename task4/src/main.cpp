#include "app.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
    using namespace metro;
    try {
        Config config;
        config.root = std::filesystem::absolute(argv[0]).parent_path().parent_path();
        const char *database = std::getenv("DATABASE_URL");
        config.database = database ? database : "host=127.0.0.1 port=55439 dbname=metro_project_cpp";
        std::string command = "serve";
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--root" || arg == "--database" || arg == "--host" || arg == "--port") {
                if (i + 1 == argc)
                    throw Error("选项缺少参数 " + arg);
                const std::string value = argv[++i];
                if (arg == "--root")
                    config.root = std::filesystem::absolute(value);
                if (arg == "--database")
                    config.database = value;
                if (arg == "--host")
                    config.host = value;
                if (arg == "--port") {
                    std::size_t end = 0;
                    config.port = std::stoi(value, &end);
                    if (end != value.size() || config.port < 1 || config.port > 65535)
                        throw Error("端口应为 1–65535。");
                }
            } else
                command = arg;
        }
        if (command == "build-import")
            build_import(config);
        else if (command == "import")
            import_database(config);
        else if (command == "init")
            import_database(config, true);
        else if (command == "serve")
            serve(config);
        else if (command == "schema")
            export_schema(config);
        else if (command == "cli")
            cli(config);
        else if (command == "help" || command == "--help")
            std::cout << "metro [--root PATH] [--host 127.0.0.1] [--port 8001] "
                         "build-import|init|import|serve|cli|schema\n数据库地址可通过 DATABASE_URL 设置。\n";
        else
            throw Error("未知命令 " + command);
    } catch (const std::exception &error) {
        std::cerr << "错误：" << error.what() << '\n';
        return 1;
    }
}
