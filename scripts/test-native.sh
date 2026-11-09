#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
mkdir -p local/native-tests
for test_path in tests/native/*.cpp; do
    test_name=$(basename "$test_path" .cpp)
    g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -Ifirmware/ecu/include "$test_path" \
        -o "local/native-tests/$test_name"
    "local/native-tests/$test_name"
done
