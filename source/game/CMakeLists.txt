project (game)

include_directories(${ANGELSCRIPT_INCLUDE_DIR})
include_directories("../../third-party/recastnavigation/Recast/Include")
include_directories("../../third-party/recastnavigation/Detour/Include")

file(GLOB GAME_HEADERS
    "*.h"
    "ai/*.h"
	"../gameshared/*.h"
	"../qalgo/base64.h"
	"../qalgo/md5.h"
	"../../third-party/recastnavigation/Recast/Include/*.h"
	"../../third-party/recastnavigation/Detour/Include/*.h"
)

file(GLOB GAME_SOURCES
    "*.cpp"
    "ai/*.cpp"
    "../gameshared/*.c"
    "../matchmaker/mm_rating.c"
	"../qalgo/base64.c"
	"../qalgo/md5.c"
	"../../third-party/recastnavigation/Recast/Source/*.cpp"
	"../../third-party/recastnavigation/Detour/Source/*.cpp"
)

add_library(game SHARED ${GAME_SOURCES} ${GAME_HEADERS})
target_link_libraries(game PRIVATE)
add_dependencies(game angelwrap)
set_property(TARGET game PROPERTY CXX_STANDARD 11)
qf_set_output_dir(game ${QFUSION_GAME_DIR})
