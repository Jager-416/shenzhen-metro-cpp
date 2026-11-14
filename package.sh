#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
make -j2
./build/metro build-import
mkdir -p submission
TEMP="$(mktemp "$PWD/submission/.source.XXXXXX.zip")"
trap 'if [ -f "$TEMP" ]; then rm -f -- "$TEMP"; fi' EXIT
zip -q -r - task1 task2 task3 task4 data third_party tests Makefile run.sh package.sh \
  README.md .clang-format .gitignore docs > "$TEMP"
mv -- "$TEMP" submission/shenzhen-metro-cpp-source.zip
unzip -tqq submission/shenzhen-metro-cpp-source.zip
printf '源码包已生成：submission/shenzhen-metro-cpp-source.zip\n'
