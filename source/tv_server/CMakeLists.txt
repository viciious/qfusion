project(${QFUSION_TVSERVER_NAME})

include_directories(${ZLIB_INCLUDE_DIR} ${CURL_INCLUDE_DIR})

file(GLOB TV_SERVER_HEADERS
    "*.h"
	"../gameshared/q_*.h"
	"../gameshared/anorms.h"
	"../gameshared/config.h"
	"../qcommon/*.h"
	"../qalgo/*.h"
)

file(GLOB TV_SERVER_SOURCES
	"../qcommon/asyncstream.c"
	"../qcommon/autoupdate.c"
	"../qcommon/cm_main.c"
	"../qcommon/cm_q3bsp.c"
	"../qcommon/cm_sample.c"
	"../qcommon/cm_trace.cpp"
	"../qcommon/cm_trace_sse42.cpp"
	"../qcommon/compression.c"
    "../qcommon/bsp.c"
    "../qcommon/patch.c"
    "../qcommon/common.c"
    "../qcommon/files.c"
    "../qcommon/cmd.c"
	"../qcommon/cjson.c"
    "../qcommon/mem.c"
    "../qcommon/net.c"
    "../qcommon/net_chan.c"
    "../qcommon/msg.c"
    "../qcommon/cvar.c"
    "../qcommon/dynvar.c"
    "../qcommon/irc.c"
    "../qcommon/library.c"
    "../qcommon/svnrev.c"
    "../qcommon/snap_demos.c"
    "../qcommon/snap_read.c"
    "../qcommon/snap_write.c"
    "../qcommon/wswcurl.c"
    "../qcommon/threads.c"
    "../qcommon/steam.c"
    "*.c"
    "../null/cl_null.c"
    "../null/ascript_null.c"
    "../null/mm_null.c"
    "../gameshared/q_*.c"
    "../qalgo/*.c"
    "tv_module/*.c"
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    file(GLOB TV_SERVER_PLATFORM_SOURCES 
        "../win32/win_fs.c"
        "../win32/win_net.c"
        "../win32/win_sys.c"
        "../win32/win_console.c"
        "../win32/win_time.c"
        "../win32/win_lib.c"
        "../win32/win_threads.c"
        "../null/sys_vfs_null.c"
        "../win32/conproc.c"
    )

    set(TV_SERVER_PLATFORM_LIBRARIES "ws2_32.lib" "winmm.lib")
    set(TV_SERVER_BINARY_TYPE WIN32)
else()
    file(GLOB TV_SERVER_PLATFORM_SOURCES 
        "../unix/unix_fs.c"
        "../unix/unix_net.c"
        "../unix/unix_sys.c"
        "../unix/unix_console.c"
        "../unix/unix_time.c"
        "../unix/unix_lib.c"
        "../unix/unix_threads.c"
        "../null/sys_vfs_null.c"
    )

    set(TV_SERVER_PLATFORM_LIBRARIES "pthread" "dl" "m")
    set(TV_SERVER_BINARY_TYPE "")
endif()

if (MSVC)
	set_source_files_properties("../qcommon/cm_trace_sse42.cpp" PROPERTIES COMPILE_FLAGS "/arch:AVX")
else()
	set_source_files_properties("../qcommon/cm_trace_sse42.cpp" PROPERTIES COMPILE_FLAGS "-msse4.2")
endif()

add_executable(${QFUSION_TVSERVER_NAME} ${TV_SERVER_BINARY_TYPE} ${TV_SERVER_HEADERS} ${TV_SERVER_SOURCES} ${TV_SERVER_PLATFORM_SOURCES})
target_link_libraries(${QFUSION_TVSERVER_NAME} PRIVATE ${CURL_LIBRARY} ${ZLIB_LIBRARY} ${TV_SERVER_PLATFORM_LIBRARIES})
qf_set_output_dir(${QFUSION_TVSERVER_NAME} "")

set_target_properties(${QFUSION_TVSERVER_NAME} PROPERTIES COMPILE_DEFINITIONS "DEDICATED_ONLY;TV_SERVER_ONLY;TV_MODULE_HARD_LINKED")