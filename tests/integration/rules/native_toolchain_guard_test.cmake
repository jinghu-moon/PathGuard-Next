if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
set(script "${SOURCE_DIR}/scripts/build-native.ps1")

execute_process(
  COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File "${script}"
    -Api 30 -Abi arm64-v8a
  RESULT_VARIABLE api_result
)
if(api_result EQUAL 0)
  message(FATAL_ERROR "native build accepted Android API 30")
endif()

execute_process(
  COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File "${script}"
    -Abi x86
  RESULT_VARIABLE abi_result
)
if(abi_result EQUAL 0)
  message(FATAL_ERROR "native build accepted a release gate without arm64-v8a")
endif()

execute_process(
  COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File "${script}"
    -NdkRoot "${SOURCE_DIR}" -Abi arm64-v8a
  RESULT_VARIABLE ndk_result
)
if(ndk_result EQUAL 0)
  message(FATAL_ERROR "native build accepted an invalid NDK root")
endif()
