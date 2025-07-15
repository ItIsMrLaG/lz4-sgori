set -euxo pipefail

if [ ! -f compile_commands.json ]; then
	bear -- make
fi

sed -i -E '/"-(m|f).*",/d' compile_commands.json
find . \( -iname '*.c' -or -iname '*.h' \) -not -path './build/*' | xargs clang-tidy --warnings-as-errors=.*
