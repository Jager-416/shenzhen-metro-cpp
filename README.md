# Shenzhen Metro Workbench · C++17

程序采用 C++17、PostgreSQL，以及现有的 HTML/CSS/JavaScript 网页。命令行和网页共用 C++ 业务代码。数据导入 SQL 由 C++ 从官方 CSV 生成。

## 启动

在已安装下列依赖的 Linux / WSL 环境中，从仓库根目录运行。

```bash
bash run.sh
```

打开 http://127.0.0.1:8001/ 。命令行入口使用 `bash run.sh cli`。API 服务按 Ctrl+C 停止，数据库用 `bash run.sh stop-db` 停止。

默认创建项目独立的 PostgreSQL 实例，监听本机 55439 端口，数据库名为 metro_project_cpp。本机运行数据保存在 .runtime/，编译产物保存在 build/。

换到另一台机器时，需要 Linux 或 WSL、C++17 编译器、GNU Make、PostgreSQL 16 服务端及 libpq.so.5 客户端库。HTTP/JSON 库与所需 libpq 头文件已保存在 third_party/。Ubuntu 24.04 可安装 g++、make、postgresql、postgresql-client。程序编译和运行使用 C++。

已有 PostgreSQL 时可指定连接字符串，先建立专用数据库。

```bash
export DATABASE_URL='host=127.0.0.1 port=5432 dbname=metro_project_cpp user=metro_user'
make -j2
./build/metro build-import
./build/metro init
./build/metro serve
```

连接信息只从环境变量或命令行读取。演示默认本机数据库采用 trust 认证，网页默认只监听 127.0.0.1。

## 按任务整理的文件

| 位置 | 内容 |
|---|---|
| task1/conceptual_er.svg | 概念 E-R 图，可用浏览器打开 |
| task2/schema.sql | 单独提交的 DDL，只包含 CREATE TABLE |
| task2/schema_diagram.svg | 从运行中 PostgreSQL 系统目录生成的结构参考图 |
| task3/import.sql | 包含全部建表、INSERT、索引和函数的独立导入 SQL |
| task3/demo_queries.sql | DataGrip 可直接执行的 Q1–Q7 示例 |
| task3/functions.sql | 指定的 get_station_n_neighbor 函数 |
| task3/queries/ | C++ 后端使用的参数化 Q1–Q7 SQL |
| task4/src/ | C++ 实现 |
| task4/include/app.hpp | 配置、连接和业务接口声明 |
| task4/web/ | 网页及字体 |
| task4/data/ | 来源可追溯的普通车厢票价矩阵 |
| data/ | 两份原始 CSV |
| tests/integration.cpp | 独立测试库上的 C++ 集成检查 |
| docs/代码导读.md | 建议从这里开始理解程序 |

## 构建与管理命令

```bash
make -j2                         # 编译
./build/metro build-import       # 从 CSV 重新生成 task3/import.sql
./build/metro init               # 仅在项目表不存在时执行导入
./build/metro import             # 重建七张项目表，复位演示数据
./build/metro serve              # 启动网页和 API
./build/metro cli                # 启动命令行菜单
./build/metro schema             # 从实际数据库导出结构参考图
make test                       # 使用一次性数据库运行检查
bash package.sh                 # 更新 submission/ 下的附件 ZIP
```

`import` 会重建项目的七张表。程序应连接专用于本项目的数据库。导入脚本采用单事务、批量 INSERT、确定性清理及身份序列复位，重复执行可回到同一份源数据。

## 数据处理选择

- 站点使用中文名与行政区组合区分。两处梨园保留两个 station_id，B、C 出口归到 Longgang。
- 原始坐标列头反置，导入时交换经纬度映射。原 CSV 保持原始内容，缺失值保存为 NULL。
- 运营状态、开通年份、首末班时刻和站序属于 line_stop，而同一实体站点在 station 中只保存一份。
- 地标按中文逗号拆分并去重。3 个空地标出口继续保留，出口关联共 3203 条。
- Q3 对运营实体站点去重，得到 350 个运营站点。Q7 返回较小站序方向、较大站序方向两行，越界返回 NULL。
- 路径查询默认只使用运营区段，可选择最少站数或最少换乘。CSV 没有换乘步行时间和闭环标识，模型采用双向相邻站连接及零步行换乘成本。
- 公布票价矩阵覆盖源数据全部 350 个运营站点，来自发改委 2026-06-22 的普通车厢票价表。CRUD 后的新增站点可能只能返回估算或缺少里程提示。票价使用公布的线网快照。

[2026 年官方票价表通知](https://fgw.sz.gov.cn/gkmlpt/content/12/12849/mpost_12849099.html)

## 开发说明

本项目使用 Codex 辅助生成、迁移和验证代码。测试程序会创建并清理一次性 PostgreSQL 数据库，需要连接用户具有 CREATEDB 权限。
