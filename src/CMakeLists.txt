add_library(sigil_core INTERFACE)

target_include_directories(sigil_core INTERFACE ${PROJECT_SOURCE_DIR}/include/daemon/)

target_link_libraries(sigil_core INTERFACE nlohmann_json::nlohmann_json)

