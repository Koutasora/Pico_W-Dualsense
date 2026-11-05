# Turns a file into a C header with a byte array.
# Usage: cmake -DIN=<file> -DOUT=<header> -DNAME=<symbol> -P embed_file.cmake
file(READ "${IN}" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LEN)
math(EXPR BYTE_LEN "${HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX_CONTENT}")
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){32}" "\\0\n" BYTES "${BYTES}")
file(WRITE "${OUT}"
"// Generated from ${IN}. Do not edit.\n#pragma once\n#include <cstddef>\n#include <cstdint>\n"
"static const uint8_t ${NAME}[] = {\n${BYTES}\n};\n"
"static const size_t ${NAME}_len = ${BYTE_LEN};\n")
