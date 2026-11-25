# Страница web/index.html -> массив C для веб-сервера V5F (WEB_Page, WEB_PageLen).
# Автор: Дмитрий (GitHub: mamkincoderr, https://github.com/mamkincoderr)
# Запуск: cmake -DIN=<html> -DOUT=<c> -P bin2c.cmake
file(READ "${IN}" HEX HEX)
string(LENGTH "${HEX}" N)
math(EXPR LEN "${N} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BODY "${HEX}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n    " BODY "${BODY}")
file(WRITE "${OUT}"
"/* Сгенерировано из web/index.html (cmake/bin2c.cmake) — не править. */\n"
"#include <stdint.h>\n\n"
"const uint8_t WEB_Page[] =\n{\n    ${BODY}\n};\n"
"const uint32_t WEB_PageLen = ${LEN};\n")
