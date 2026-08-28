cmake_minimum_required(VERSION 3.22)
project(CreationSuiteCel LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../CMake/CelGrammar.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/CreationSharedLLVM.cmake")

ce_add_cel_grammar(CE_CEL_GRAMMAR_SOURCES)

add_library(creation_suite_cel_frontend STATIC
    include/lang/source_location.h
    include/lang/diagnostics.h
    include/lang/type.h
    include/lang/ast.h
    include/lang/ast_printer.h
    include/lang/lexer.h
    include/lang/compiler.h
    include/lang/sema.h
    include/lang/intrinsics.def
    src/source_location.cpp
    src/diagnostics.cpp
    src/type.cpp
    src/ast.cpp
    src/ast_printer.cpp
    src/compiler.cpp
    src/sema.cpp
    ${CE_CEL_GRAMMAR_SOURCES}
)

target_include_directories(creation_suite_cel_frontend
    PUBLIC include
    PRIVATE "${CE_CEL_GRAMMAR_GEN_DIR}" "${CE_FLEXLEXER_INCLUDE_DIR}"
)

target_compile_features(creation_suite_cel_frontend PUBLIC cxx_std_20)
set_source_files_properties(${CE_CEL_GRAMMAR_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")

if(NOT TARGET ce_lang_frontend)
    add_library(ce_lang_frontend ALIAS creation_suite_cel_frontend)
endif()

add_library(creation_suite_cel_nodegen STATIC
    include/lang/nodegen/node_catalog.h
    src/nodegen/node_catalog.cpp
    include/lang/nodegen/graph_to_source.h
    src/nodegen/graph_to_source.cpp
    include/lang/nodegen/foley_node_catalog.h
    src/nodegen/foley_node_catalog.cpp
    include/lang/nodegen/foley_graph_to_source.h
    src/nodegen/foley_graph_to_source.cpp
)

target_include_directories(creation_suite_cel_nodegen PUBLIC include)
target_link_libraries(creation_suite_cel_nodegen
    PUBLIC
        creation_suite_cel_frontend
        creation_suite_node_system
)
target_compile_features(creation_suite_cel_nodegen PUBLIC cxx_std_20)

if(NOT TARGET ce_lang_nodegen)
    add_library(ce_lang_nodegen ALIAS creation_suite_cel_nodegen)
endif()

add_executable(creation_suite_cel_frontend_smoke
    tests/CelFrontendSmoke.cpp
)

target_link_libraries(creation_suite_cel_frontend_smoke
    PRIVATE
        creation_suite_cel_frontend
)

add_test(
    NAME creation_suite_cel_frontend_smoke
    COMMAND creation_suite_cel_frontend_smoke
)

option(CREATION_SUITE_CEL_ENABLE_JIT "Build the shared CEL LLVM JIT and celc tool" ON)

if(CREATION_SUITE_CEL_ENABLE_JIT)
    configure_creation_shared_llvm(CREATION_SUITE_CEL "Creation Suite CEL")
endif()

if(CREATION_SUITE_CEL_ENABLE_JIT AND LLVM_FOUND)
    llvm_map_components_to_libnames(CREATION_SUITE_CEL_LLVM_LIBS
        Support
        Core
        OrcJIT
        native
        X86CodeGen
        X86AsmParser
        X86Desc
        X86Info
    )

    # Deliberately app-agnostic: no EnTT, no EngineCore, no World/Entity
    # knowledge anywhere in this target. `script_context.h` is the ONLY
    # context type this library knows about. A consuming app (Creation
    # Engine) builds its own execution/trampoline layer on top of this
    # (see apps/CreationEngine/Language's world_intrinsics/world_runtime),
    # linking against creation_suite_cel_jit rather than this target
    # growing app-specific code.
    add_library(creation_suite_cel_jit STATIC
        include/lang/jit/runtime.h
        include/lang/jit/script_context.h
        src/jit/runtime.cpp
        src/jit/optimizer.h
        src/jit/optimizer.cpp
        src/jit/module_builder.h
        src/jit/module_builder.cpp
        src/jit/intrinsic_trampolines.h
        src/jit/intrinsic_trampolines.cpp
        src/jit/abi_registration.h
        src/jit/abi_registration.cpp
    )

    target_include_directories(creation_suite_cel_jit
        PUBLIC include
        PRIVATE ${LLVM_INCLUDE_DIRS}
    )
    target_compile_definitions(creation_suite_cel_jit PRIVATE ${LLVM_DEFINITIONS} NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(creation_suite_cel_jit
        PUBLIC
            creation_suite_cel_frontend
            ${CREATION_SUITE_CEL_LLVM_LIBS}
    )
    target_compile_features(creation_suite_cel_jit PUBLIC cxx_std_20)

    if(NOT TARGET ce_lang_jit)
        add_library(ce_lang_jit ALIAS creation_suite_cel_jit)
    endif()

    add_executable(creation_suite_cel_foley_smoke
        tests/FoleyCelSmoke.cpp
    )
    target_link_libraries(creation_suite_cel_foley_smoke
        PRIVATE
            creation_suite_cel_frontend
            creation_suite_cel_nodegen
            creation_suite_cel_jit
    )
    add_test(
        NAME creation_suite_cel_foley_smoke
        COMMAND creation_suite_cel_foley_smoke
    )
endif()
