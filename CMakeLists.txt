cmake_minimum_required(VERSION 3.20)

project(MiniCCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIC_ENABLE_LLVM "Try to discover and link LLVM for future codegen work" ON)

add_library(minic_core
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
    src/codegen.cpp
)
target_include_directories(minic_core PUBLIC include)
target_compile_features(minic_core PUBLIC cxx_std_17)

if(MINIC_ENABLE_LLVM)
    find_package(LLVM CONFIG QUIET)
    if(LLVM_FOUND)
        message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
        target_compile_definitions(minic_core PUBLIC MINIC_HAS_LLVM=1)
        target_include_directories(minic_core SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
        separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
        target_compile_definitions(minic_core PUBLIC ${LLVM_DEFINITIONS_LIST})
        llvm_map_components_to_libnames(LLVM_LIBS core support irreader)
        target_link_libraries(minic_core PUBLIC ${LLVM_LIBS})
    else()
        message(STATUS "LLVM not found. Lexer/parser setup will still build.")
    endif()
endif()

add_executable(minic src/main.cpp)
target_link_libraries(minic PRIVATE minic_core)

enable_testing()

add_executable(lexer_test tests/lexer_test.cpp)
target_link_libraries(lexer_test PRIVATE minic_core)
add_test(NAME lexer_test COMMAND lexer_test)

add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE minic_core)
add_test(NAME smoke_test COMMAND smoke_test)
