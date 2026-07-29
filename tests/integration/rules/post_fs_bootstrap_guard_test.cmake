if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/module/post-fs-data.sh" bootstrap)
foreach(required IN ITEMS
    "pathguardctl"
    "compile \"$MODDIR/config/rules.toml\" \"$BOOTSTRAP\""
    "mv -f \"$BOOTSTRAP\" \"$POLICY\"")
  string(FIND "${bootstrap}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "post-fs-data bootstrap is missing: ${required}")
  endif()
endforeach()

if(bootstrap MATCHES "pathguardd.*--compile")
  message(FATAL_ERROR "post-fs-data must compile the bootstrap policy without topology admission")
endif()
