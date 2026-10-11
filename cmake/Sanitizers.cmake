# AddressSanitizer + UndefinedBehaviorSanitizer for the Debug/test presets. Applied globally (not per
# target) because every object in the process, including the test binaries, must be instrumented alike.

if(FILEOPS_ENABLE_SANITIZERS)
  if(FILEOPS_STATIC_BINARY)
    message(FATAL_ERROR "FILEOPS_ENABLE_SANITIZERS and FILEOPS_STATIC_BINARY are mutually exclusive: sanitizer runtimes link dynamically")
  endif()
  set(_fileops_san -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_compile_options(${_fileops_san})
  add_link_options(${_fileops_san})
  message(STATUS "fileops: AddressSanitizer and UndefinedBehaviorSanitizer enabled")
endif()
