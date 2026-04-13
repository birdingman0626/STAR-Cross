# xxd.cmake - Replicate `xxd -i` functionality
# Converts a binary file into a C header with an unsigned char array
#
# Usage: cmake -DINPUT_FILE=<file> -DOUTPUT_FILE=<file> -P xxd.cmake

if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE must be defined")
endif()

# Read the file as hex
file(READ "${INPUT_FILE}" FILE_CONTENTS HEX)

# Get the variable name from the file name (basename without extension)
get_filename_component(VAR_NAME "${INPUT_FILE}" NAME)
string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" VAR_NAME "${VAR_NAME}")

# Get file size
file(SIZE "${INPUT_FILE}" FILE_SIZE)

# Convert hex string to comma-separated 0xNN values
string(LENGTH "${FILE_CONTENTS}" HEX_LENGTH)
set(OUTPUT_ARRAY "")
set(LINE_COUNTER 0)
math(EXPR LAST_INDEX "${HEX_LENGTH} - 2")

foreach(IDX RANGE 0 ${LAST_INDEX} 2)
    string(SUBSTRING "${FILE_CONTENTS}" ${IDX} 2 HEX_BYTE)
    if(LINE_COUNTER EQUAL 0)
        string(APPEND OUTPUT_ARRAY "  ")
    endif()

    math(EXPR REMAINING "${LAST_INDEX} - ${IDX}")
    if(REMAINING GREATER 0)
        string(APPEND OUTPUT_ARRAY "0x${HEX_BYTE}, ")
    else()
        string(APPEND OUTPUT_ARRAY "0x${HEX_BYTE}")
    endif()

    math(EXPR LINE_COUNTER "${LINE_COUNTER} + 1")
    if(LINE_COUNTER EQUAL 12)
        string(APPEND OUTPUT_ARRAY "\n")
        set(LINE_COUNTER 0)
    endif()
endforeach()

# Write the output file
file(WRITE "${OUTPUT_FILE}"
    "unsigned char ${VAR_NAME}[] = {\n${OUTPUT_ARRAY}\n};\nunsigned int ${VAR_NAME}_len = ${FILE_SIZE};\n"
)
