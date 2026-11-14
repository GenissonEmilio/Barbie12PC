# Inject the desktop target after the pinned PS2RetroX project defines its runtime.
function(barbie_add_desktop)
    if(NOT BARBIE_BATCH_DIR)
        return()
    endif()
    foreach(path BARBIE_BATCH_DIR BARBIE_GENERATED_DIR BARBIE_SOURCE_DIR)
        cmake_path(CONVERT "${${path}}" TO_CMAKE_PATH_LIST ${path} NORMALIZE)
    endforeach()
    file(GLOB batches CONFIGURE_DEPENDS "${BARBIE_BATCH_DIR}/functions-*.cpp")
    if(NOT batches)
        message(FATAL_ERROR "Generate the Barbie sources before configuring the desktop target.")
    endif()

    get_target_property(runtime_sources ps2_runtime SOURCES)
    list(FILTER runtime_sources EXCLUDE REGEX "(^|/)(gow2_desktop|desktop_video|Dispatcher|EeScheduler|MPEG|gs_hw_backend|gs_frontend|ps2_memory)\\.cpp$")
    set_property(TARGET ps2_runtime PROPERTY SOURCES "${runtime_sources}")
    target_sources(ps2_runtime PRIVATE
        "${BARBIE_BATCH_DIR}/Dispatcher.cpp"
        "${BARBIE_BATCH_DIR}/../video/EeScheduler.cpp"
        "${BARBIE_BATCH_DIR}/../video/MPEG.cpp"
        "${BARBIE_BATCH_DIR}/../video/gs_hw_backend.cpp"
        "${BARBIE_BATCH_DIR}/../video/gs_frontend.cpp"
        "${BARBIE_BATCH_DIR}/../video/ps2_memory.cpp")
    set(kernel "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel")
    target_include_directories(ps2_runtime PRIVATE "${kernel}/Stubs" "${kernel}/Syscalls"
        "${kernel}/.." "${kernel}/../gs" "${kernel}/../gs/gpu")

    get_target_property(iop_sources ps2_iop SOURCES)
    list(FILTER iop_sources EXCLUDE REGEX "(^|/)(iop_memory|iop_emulator|iop_module_loader)\\.cpp$")
    set_property(TARGET ps2_iop PROPERTY SOURCES "${iop_sources}")
    set(iop_original "${CMAKE_SOURCE_DIR}/ps2xIOP/src/emulator")
    set(iop_overlay "${BARBIE_BATCH_DIR}/../iop")
    target_sources(ps2_iop PRIVATE
        "${iop_overlay}/core/iop_memory.cpp"
        "${iop_overlay}/iop_emulator.cpp"
        "${iop_overlay}/services/iop_module_loader.cpp")
    target_include_directories(ps2_iop PRIVATE
        "${iop_original}" "${iop_original}/core" "${iop_original}/services")

    if(WIN32)
        # The upstream declaration for this function is Linux-only.
        set_property(SOURCE "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/gs/gs_host_present.cpp"
            TARGET_DIRECTORY ps2_runtime APPEND PROPERTY COMPILE_OPTIONS
            "/FI${BARBIE_SOURCE_DIR}/presentation.h")
    endif()

    add_executable(Barbie12PC ${batches}
        "${BARBIE_BATCH_DIR}/register_functions.cpp"
        "${BARBIE_SOURCE_DIR}/main.cpp"
        "${BARBIE_SOURCE_DIR}/movie.cpp"
        "${BARBIE_SOURCE_DIR}/presentation.cpp")
    target_compile_features(Barbie12PC PRIVATE cxx_std_20)
    target_include_directories(Barbie12PC PRIVATE "${BARBIE_GENERATED_DIR}")
    target_link_libraries(Barbie12PC PRIVATE ps2_runtime)
    if(MSVC)
        target_compile_options(Barbie12PC PRIVATE /bigobj /utf-8 /FS /Od)
        target_link_options(Barbie12PC PRIVATE /FORCE:MULTIPLE)
    else()
        target_compile_options(Barbie12PC PRIVATE -O0 -msse4.1 -fno-strict-aliasing)
        target_compile_options(ps2_runtime PRIVATE -msse4.1 -fno-strict-aliasing)
    endif()
endfunction()

cmake_language(DEFER CALL barbie_add_desktop)
