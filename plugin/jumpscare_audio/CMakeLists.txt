cmake_minimum_required(VERSION 3.20)
project(jumpscare_audio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so the DLL has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# REFramework embeds stock Lua 5.4.3 (dependencies/lua in the REFramework repo is
# byte-identical to upstream for luaconf.h / lstate.h / lobject.h). We operate on
# REFramework's lua_State with our own copy of the Lua code, so the version MUST match.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.3.tar.gz
    URL_HASH SHA256=f8612276169e3bfcbcfb8f226195bfc6e466fe13042f1076cbde92b7ec96bbfb
)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR}/src)

add_library(jumpscare_audio SHARED src/main.cpp)
target_include_directories(jumpscare_audio PRIVATE include)
target_link_libraries(jumpscare_audio PRIVATE lua_static winmm)
target_compile_definitions(jumpscare_audio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
