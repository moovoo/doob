#!/usr/bin/env bash
# clang-tidy wrapper for doob.
#
# The distro's Qt6 CMake package adds -mno-direct-extern-access to every
# target built with GCC. The clang driver used by clang-tidy does not know
# that flag and reports it as an error (and exits non-zero even when the
# code is clean), so we run clang-tidy against a copy of the compile
# database with the flag stripped out.
#
# Usage:
#   cmake -S . -B build            # generates build/compile_commands.json
#   scripts/clang-tidy.sh file.cpp [more files...]
#
# With no file arguments, all project sources (and tests) are checked.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="$ROOT/build/compile_commands.json"

if [[ ! -f "$DB" ]]; then
    echo "error: $DB not found; run 'cmake -S . -B build' first" >&2
    exit 1
fi

db_dir="$(mktemp -d)"
trap 'rm -rf "$db_dir"' EXIT

python3 - "$DB" "$db_dir/compile_commands.json" <<'EOF'
import json, sys

db = json.load(open(sys.argv[1]))
for entry in db:
    if "command" in entry:
        entry["command"] = " ".join(
            arg for arg in entry["command"].split()
            if arg != "-mno-direct-extern-access"
        )
    elif "arguments" in entry:
        entry["arguments"] = [
            arg for arg in entry["arguments"]
            if arg != "-mno-direct-extern-access"
        ]
json.dump(db, open(sys.argv[2], "w"), indent=2)
EOF

if [[ $# -eq 0 ]]; then
    files=("$ROOT"/*.cpp "$ROOT"/tests/*.cpp "$ROOT"/plugins/*/tests/*.cpp)
else
    files=("$@")
fi

exec clang-tidy -p "$db_dir" "${files[@]}"
