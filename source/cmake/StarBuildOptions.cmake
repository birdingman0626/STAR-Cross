# Keep production and test compilation consistent without applying flags to
# FetchContent projects. Sanitizer link flags propagate from static libraries.
function(star_target_build_options target)
    target_compile_definitions(${target} PRIVATE
        STAR_VERSION="STAR-Cross 0.0.1_${GIT_COMMIT_HASH}")
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /utf-8
            $<$<CONFIG:Debug>:/Od>
            $<$<NOT:$<CONFIG:Debug>>:/O2>
            $<$<NOT:$<CONFIG:Debug>>:/Ob2>
            $<$<NOT:$<CONFIG:Debug>>:/Oi>)
    else()
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-O0>
            $<$<NOT:$<CONFIG:Debug>>:-O3>)
    endif()
endfunction()

function(star_target_sanitizers target)
    if(STAR_ASAN)
        if(MSVC)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>)
        else()
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>
                $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>)
            target_link_options(${target} PUBLIC -fsanitize=address)
        endif()
    endif()
    if(STAR_UBSAN)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=undefined>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>)
        target_link_options(${target} PUBLIC -fsanitize=undefined -fno-sanitize-recover=undefined)
    endif()
endfunction()
