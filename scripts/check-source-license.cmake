# SPDX-License-Identifier: MIT
# Portable source-only license check: no Git or Python required.
if(NOT DEFINED PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/LICENSE" _license)
if(NOT _license MATCHES "MIT License" OR NOT _license MATCHES "Babii Eduard")
  message(FATAL_ERROR "The root LICENSE must contain MIT License and copyright")
endif()
file(GLOB_RECURSE _sources LIST_DIRECTORIES FALSE
    "${PROJECT_ROOT}/src/*.cpp"
    "${PROJECT_ROOT}/include/*.hpp"
    "${PROJECT_ROOT}/include/*.in"
    "${PROJECT_ROOT}/tests/*.cpp")
if(NOT _sources)
  message(FATAL_ERROR "No C++ source files found")
endif()
foreach(_source IN LISTS _sources)
  file(STRINGS "${_source}" _firstLine LIMIT_COUNT 1)
  if(NOT _firstLine STREQUAL "// SPDX-License-Identifier: MIT")
    message(FATAL_ERROR "Missing MIT SPDX header: ${_source}")
  endif()
endforeach()
message(STATUS "MIT LICENSE and source headers validated")
