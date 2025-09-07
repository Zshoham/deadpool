function(generate_config_helpers)
  cmake_parse_arguments(PARSE_ARGV 0 arg "OPTIONAL" "CONFIG_HEADER;GENERATED_HEADER_DIR" "")
  if(NOT DEFINED arg_CONFIG_HEADER OR NOT EXISTS ${arg_CONFIG_HEADER})
    message(FATAL_ERROR "variable CONFIG_HEADER must be a valid header file.")
  endif()
  if(NOT DEFINED arg_GENERATED_HEADER_DIR OR NOT EXISTS ${arg_GENERATED_HEADER_DIR})
    set(arg_GENERATED_HEADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/config_headers)
    set(GENERATED_HEADER_DIR ${arg_GENERATED_HEADER_DIR})
  endif()

  file(MAKE_DIRECTORY ${arg_GENERATED_HEADER_DIR})

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_custom_command(
    OUTPUT "${arg_GENERATED_HEADER_DIR}/config_macros.h"
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_config.py"
      "${arg_CONFIG_HEADER}"
      "${arg_GENERATED_HEADER_DIR}/config_macros.h"
    DEPENDS "${arg_CONFIG_HEADER}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_config.py"
    COMMENT "Generating config helper macros from ${arg_CONFIG_HEADER}"
    VERBATIM
  )
  add_custom_target(gen_config_headers DEPENDS "${arg_GENERATED_HEADER_DIR}/config_macros.h")
  return(PROPAGATE GENERATED_HEADER_DIR)
endfunction()
