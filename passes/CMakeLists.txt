set(PLUGIN_NAME LoggerPass)

set(SOURCES
	LoggerPass.cpp
)

# Prefer modern helper when available
if(COMMAND add_llvm_pass_plugin)
    add_llvm_pass_plugin(${PLUGIN_NAME}
        ${SOURCES}
    )
else()
    add_library(${PLUGIN_NAME} MODULE ${SOURCES})
    # Link against all LLVM components as a fallback
    llvm_map_components_to_libnames(llvm_libs support core irreader passes)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${llvm_libs})
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        PREFIX ""
    )
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(${PLUGIN_NAME} PRIVATE ${LLVM_DEFINITIONS})


