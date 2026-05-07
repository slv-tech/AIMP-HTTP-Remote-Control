# AIMP-Plugin


x86_64-w64-mingw32-g++ \
    -std=c++17 -O2 -Wall \
    -Wno-missing-braces \
    -D_WIN32_WINNT=0x0A00 \
    -I. -Isdk -Ithird_party \
    plugin_minimal.cpp \
    -shared -o AimpHttpControl64_static.dll \
    -lws2_32 -luuid -lkernel32 -luser32 \
    -static-libgcc -static-libstdc++ \
    -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic




