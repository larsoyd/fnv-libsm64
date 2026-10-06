# copies a source file with the function definitions in NAMES (old=new pairs) renamed
file(READ "${IN}" text)
foreach(pair IN LISTS NAMES)
    string(REPLACE "=" ";" pair "${pair}")
    list(GET pair 0 old)
    list(GET pair 1 new)
    # the caller in the same file saw no declaration before, so the old name gets one
    string(REGEX REPLACE "\n([a-z0-9_]+) ${old}\\(([^)]*)\\)" "\n\\1 ${old}(\\2);\n\\1 ${new}(\\2)" text "${text}")
    if(NOT text MATCHES "\n[a-z0-9_]+ ${new}\\(")
        message(FATAL_ERROR "no definition of ${old} in ${IN}")
    endif()
endforeach()
file(WRITE "${OUT}" "${text}")
