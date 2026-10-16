# PASS_REGULAR_EXPRESSION ignores the process exit code, including a normal LSan
# failure after successful rendering. Require both the output and a clean exit.
set(capture_args)
if(DEFINED CAPTURE_TICK)
    list(APPEND capture_args --capture-tick "${CAPTURE_TICK}" --capture-out "${CAPTURE_OUT}")
endif()
execute_process(COMMAND "${GAME}" --replay "${REPLAY}" ${capture_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(APPEND output "${errors}")
message("${output}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Replay process failed: ${result}")
endif()
if(NOT output MATCHES "${EXPECTED}" OR
   NOT output MATCHES "Shutdown complete; Vulkan validation errors: 0")
    message(FATAL_ERROR "Replay did not reach the expected state and clean Vulkan shutdown")
endif()
