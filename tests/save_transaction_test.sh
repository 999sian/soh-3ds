#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp /tmp/soh-save-test-XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror "$project_dir/tests/save_transaction_test.cpp" \
  -Wl,--wrap=fopen,--wrap=fwrite,--wrap=fread,--wrap=ferror,--wrap=fclose,--wrap=fflush,--wrap=fsync,--wrap=rename,--wrap=remove \
  -o "$test_binary"
"$test_binary"
