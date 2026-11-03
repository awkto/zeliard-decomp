cmake_minimum_required(VERSION 3.18)
project(zeliard_port C)

# SDL2 (pinned, fetched by packaging/android/fetch-sdl.sh) builds as libSDL2.so
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
get_filename_component(SDL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../../sdl/src" ABSOLUTE)
get_filename_component(PORT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../port" ABSOLUTE)
add_subdirectory(${SDL_SRC} SDL2-build)

# The `zeliard` binary's object list from port/Makefile (CORE + SDL_ONLY + input
# + SYNC_OBJ + main/trace/syncdlg), with net_stub.c instead of net_curl.c.
# No test_*.c.  Keep in step with the Makefile.
set(SRCS main trace
    sar gfx map physics render png enemy ai combat shots sound
    ai_eai1 ai_eai2 ai_eai3 ai_eai4 ai_eai5 ai_eai6 ai_eai7 ai_eai8 town
    boss boss_crab boss_tako boss_tori boss_zela boss_meda boss_lega
    boss_drgn boss_akma boss_mao1 boss_mao2 boss_generic
    text player shop status shell savestate nav playthrough opl2 msd audio
    config statekeys restore gd cutscene tear
    video video_mcga video_cga video_ega video_hgc video_tandy
    present touch input sync synclife syncdlg net_stub)
set(SRC_FILES "")
foreach(s ${SRCS})
    list(APPEND SRC_FILES "${PORT}/${s}.c")
endforeach()
list(APPEND SRC_FILES "${PORT}/third_party/miniz/miniz.c")
# as the Makefile builds it: silent, no stdio
set_source_files_properties("${PORT}/third_party/miniz/miniz.c" PROPERTIES
    COMPILE_FLAGS "-w" COMPILE_DEFINITIONS "MINIZ_NO_STDIO")

# SDL_main (SDL_main.h turns main() into SDL_main on Android) lives in libmain.so
add_library(main SHARED ${SRC_FILES})
target_compile_definitions(main PRIVATE HAVE_SDL)
target_compile_options(main PRIVATE -O2 -Wall -Wno-unused-parameter -Wno-unused-function)
target_include_directories(main PRIVATE ${PORT} ${PORT}/third_party/miniz)
set_target_properties(main PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(main PRIVATE SDL2 log m)
