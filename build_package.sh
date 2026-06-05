#!/usr/bin/env bash
# Собирает x64 и x32 DLL и упаковывает в .aimppack
# Структура пакета согласно документации AIMP SDK:
#   AimpHttpControl/
#       AimpHttpControl.dll       <- 32-bit
#       x64/
#           AimpHttpControl.dll   <- 64-bit
set -e

BUILD_FLAGS="-std=c++17 -O2 -Wall \
    -Wno-missing-braces -Wno-delete-non-virtual-dtor \
    -D_WIN32_WINNT=0x0A00 \
    -Isrc -isystem sdk -isystem third_party \
    src/globals.cpp src/utils.cpp src/network.cpp src/focus_sync.cpp \
    src/settings.cpp src/player_api.cpp src/http_server.cpp \
    src/options_frame.cpp src/plugin.cpp"

LINK_FLAGS="-lws2_32 -liphlpapi -luuid -lkernel32 -luser32 -lgdi32 \
    -static-libgcc -static-libstdc++ \
    -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic"

echo "Building x64..."
x86_64-w64-mingw32-g++ $BUILD_FLAGS \
    -shared -o AimpHttpControl64.dll $LINK_FLAGS

echo "Building x32..."
i686-w64-mingw32-g++ $BUILD_FLAGS \
    -shared -o AimpHttpControl32.dll $LINK_FLAGS \
    src/plugin.def

echo "Packaging .aimppack..."
rm -rf /tmp/aimppack_build
mkdir -p /tmp/aimppack_build/AimpHttpControl/x64/Langs
mkdir -p /tmp/aimppack_build/AimpHttpControl/Langs
cp AimpHttpControl32.dll /tmp/aimppack_build/AimpHttpControl/AimpHttpControl.dll
cp AimpHttpControl64.dll /tmp/aimppack_build/AimpHttpControl/x64/AimpHttpControl.dll
# Langs/ в корне — для 32-бит; Langs/ в x64/ — для 64-бит
cp Langs/english.lng /tmp/aimppack_build/AimpHttpControl/Langs/english.lng
cp Langs/english.lng /tmp/aimppack_build/AimpHttpControl/x64/Langs/english.lng
# russian.lng хранится в UTF-8, конвертируем в CP1251 для AIMP
iconv -f UTF-8 -t CP1251 Langs/russian.lng -o /tmp/aimppack_build/AimpHttpControl/Langs/russian.lng
iconv -f UTF-8 -t CP1251 Langs/russian.lng -o /tmp/aimppack_build/AimpHttpControl/x64/Langs/russian.lng

cd /tmp/aimppack_build
zip -r AimpHttpControl.aimppack AimpHttpControl/
cd - > /dev/null
cp /tmp/aimppack_build/AimpHttpControl.aimppack ./AimpHttpControl.aimppack

echo "Done: AimpHttpControl.aimppack"
echo ""
unzip -l AimpHttpControl.aimppack
