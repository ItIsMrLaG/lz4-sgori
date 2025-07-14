find . \( -iname '*.c' -or -iname '*.h' \) -not -path './build/*' | xargs clang-format --verbose -i
