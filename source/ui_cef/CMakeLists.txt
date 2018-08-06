project(ui_cef)

file(GLOB UI_CEF_HEADERS "*.h")
file(GLOB UI_CEF_SOURCES "*.cpp" "syscalls/*.cpp" "../gameshared/q_math.c" "../gameshared/q_shared.c")

include_directories(${CEF_INCLUDE_DIR})
link_directories(${CEF_LINK_DIR})

add_library(ui_cef SHARED ${UI_CEF_HEADERS} ${UI_CEF_SOURCES})
target_link_libraries(ui_cef)
target_link_libraries(ui_cef PRIVATE libcef_dll_wrapper)
qf_set_output_dir(ui_cef "")
set_property(TARGET ui_cef PROPERTY CXX_STANDARD 14)

add_executable(ui_cef_process ${UI_CEF_HEADERS} ${UI_CEF_SOURCES})
target_link_libraries(ui_cef_process)
target_link_libraries(ui_cef_process PRIVATE libcef_dll_wrapper)
qf_set_output_dir(ui_cef_process "")
set_property(TARGET ui_cef_process PROPERTY CXX_STANDARD 14)