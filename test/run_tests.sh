#!/bin/bash
# quil test runner — compiles each test/programs/*.qil and checks exit code
# against the `// expect: N` line in the file.
# Usage: ./test/run_tests.sh
set -u
cd "$(dirname "$0")/.."

if [ ! -f "bin/quil" ]; then
        echo "Error: bin/quil not found. Run 'make' first."
        exit 1
fi

pass=0
fail=0
mkdir -p payload/tests
for file in test/programs/*.qil; do
        name=$(basename "$file" .qil)
        expect=$(grep -m1 -oP '^// expect:\s*\K-?[0-9]+' "$file" || echo "")
        if [ -z "$expect" ]; then
                echo "SKIP $name (no '// expect: N' line)"
                continue
        fi
        if ! ./bin/quil -o "payload/tests/$name" "$file" >/dev/null 2>&1; then
                echo "FAIL $name (compile failed)"
                fail=$((fail + 1))
                continue
        fi
        "payload/tests/$name" >/dev/null 2>&1
        got=$?
        if [ "$got" -eq "$expect" ]; then
                echo "PASS $name (exit $got)"
                pass=$((pass + 1))
        else
                echo "FAIL $name (expected $expect, got $got)"
                fail=$((fail + 1))
        fi
done
echo "---"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
