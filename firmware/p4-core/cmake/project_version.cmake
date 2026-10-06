# M3 has its own release ancestry. Explicit release versions are bounded before
# ESP-IDF generates the signed application's fixed-size descriptor.
function(pajoniiir_resolve_version tag_pattern)
    find_package(Git REQUIRED)
    if(PAJONIIIR_RELEASE_VERSION)
        set(version "${PAJONIIIR_RELEASE_VERSION}")
        if(NOT version MATCHES "^M3-[0-9]+")
            message(FATAL_ERROR "M3 release requires an explicit M3 numeric version")
        endif()
    else()
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty
            --match "${tag_pattern}" --exclude "*-g*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
            RESULT_VARIABLE result OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT result EQUAL 0)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                RESULT_VARIABLE result OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "Cannot identify M3 development source")
            endif()
            set(version "M3-dev-g${sha}")
        endif()
    endif()
    string(LENGTH "${version}" bytes)
    if(bytes GREATER 31)
        message(FATAL_ERROR "ESP-IDF app descriptors allow at most 31 bytes")
    endif()
    set(PROJECT_VER "${version}" PARENT_SCOPE)
endfunction()
