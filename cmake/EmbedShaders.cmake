include_guard(GLOBAL)

# Resolve quoted, relative HLSL includes before embedding. The compiler receives
# self-contained source, so no shader files are needed beside the executable.
# This is textual expansion, not an HLSL preprocessor: conditionals and macros
# remain in the source for D3DCompile. Include filenames must be literal strings.
function(city_expand_shader path root stack result)
    get_filename_component(path "${path}" ABSOLUTE)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Shader source does not exist: ${path}")
    endif()
    file(REAL_PATH "${path}" path)
    if(path IN_LIST stack)
        message(FATAL_ERROR "Circular shader include: ${stack};${path}")
    endif()
    list(APPEND stack "${path}")

    # Ordinary `cmake --build` notices edits to both entry points and includes.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" remaining)
    string(REPLACE "\r\n" "\n" remaining "${remaining}")
    get_filename_component(directory "${path}" DIRECTORY)
    file(RELATIVE_PATH label "${root}" "${path}")
    set(label "shaders/${label}")
    set(expanded "#line 1 \"${label}\"\n")
    set(line_number 1)

    # Work on strings, not CMake lists: HLSL statements contain semicolons.
    while(NOT remaining STREQUAL "")
        string(FIND "${remaining}" "\n" newline)
        if(newline EQUAL -1)
            set(line "${remaining}")
            set(remaining "")
        else()
            math(EXPR length "${newline} + 1")
            string(SUBSTRING "${remaining}" 0 ${length} line)
            string(SUBSTRING "${remaining}" ${length} -1 remaining)
        endif()
        math(EXPR line_number "${line_number} + 1")

        if(line MATCHES "^[ \t]*#[ \t]*include[ \t]+\"([^\"\r\n]+)\"[ \t]*(//[^\r\n]*)?[\r\n]*$")
            set(include_path "${directory}/${CMAKE_MATCH_1}")
            city_expand_shader("${include_path}" "${root}" "${stack}" included)
            string(APPEND expanded "${included}\n#line ${line_number} \"${label}\"\n")
        elseif(line MATCHES "^[ \t]*#[ \t]*include([ \t]|<|\")")
            message(FATAL_ERROR "Use a quoted, relative #include in ${path}: ${line}")
        else()
            string(APPEND expanded "${line}")
        endif()
    endwhile()
    set(${result} "${expanded}" PARENT_SCOPE)
endfunction()

# Arguments after the header name are pairs: C++ symbol, HLSL entry-point path.
function(city_embed_shader_group target header)
    set(entries ${ARGN})
    list(LENGTH entries count)
    math(EXPR remainder "${count} % 2")
    if(remainder OR count EQUAL 0)
        message(FATAL_ERROR "Shader group ${header} needs symbol/path pairs")
    endif()

    set(CITY_SHADER_DECLARATIONS "")
    while(entries)
        list(POP_FRONT entries symbol source)
        city_expand_shader("${CMAKE_CURRENT_SOURCE_DIR}/${source}"
            "${CMAKE_CURRENT_SOURCE_DIR}" "" expanded)
        string(FIND "${expanded}" ")city_hlsl\"" delimiter)
        if(NOT delimiter EQUAL -1)
            message(FATAL_ERROR "Shader ${source} contains the reserved raw-string delimiter )city_hlsl\"")
        endif()
        string(APPEND CITY_SHADER_DECLARATIONS
            "inline constexpr char ${symbol}[] = R\"city_hlsl(${expanded})city_hlsl\";\n\n")
    endwhile()

    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded/${header}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ShaderHeader.hpp.in"
        "${output}" @ONLY NEWLINE_STYLE LF)
    # Also recover if a generated header is removed without deleting the cache.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${output}")
    target_sources(${target} INTERFACE "${output}")
endfunction()
