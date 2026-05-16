if(NOT DFTRACER_GENERATE_INTERFACES)
  return()
endif()

set(_dftracer_generator_python "")
if(DEFINED DFTRACER_PYTHON_EXE AND NOT "${DFTRACER_PYTHON_EXE}" STREQUAL "")
  set(_dftracer_generator_python "${DFTRACER_PYTHON_EXE}")
elseif(DEFINED PYTHON_EXECUTABLE AND NOT "${PYTHON_EXECUTABLE}" STREQUAL "")
  set(_dftracer_generator_python "${PYTHON_EXECUTABLE}")
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_dftracer_generator_python "${Python3_EXECUTABLE}")
endif()

if(NOT EXISTS "${_dftracer_generator_python}")
  message(FATAL_ERROR "Python interpreter for interface generator not found: ${_dftracer_generator_python}")
endif()

set(_generator_script "${CMAKE_CURRENT_SOURCE_DIR}/generate_interfaces_from_brahma.py")
if(NOT EXISTS "${_generator_script}")
  message(FATAL_ERROR "DFTracer interface generator script not found: ${_generator_script}")
endif()

set(_requested_interfaces "")
if(DFTRACER_ENABLE_HDF5)
  list(APPEND _requested_interfaces hdf5)
endif()
if(DFTRACER_ENABLE_MPI)
  list(APPEND _requested_interfaces mpi mpiio)
endif()

if("${_requested_interfaces}" STREQUAL "")
  message(FATAL_ERROR
    "DFTRACER_GENERATE_INTERFACES=ON requires at least one enabled interface "
    "(set DFTRACER_ENABLE_HDF5 and/or DFTRACER_ENABLE_MPI).")
endif()

set(_brahma_required_headers "")
foreach(_iface IN LISTS _requested_interfaces)
  list(APPEND _brahma_required_headers "brahma/interface/${_iface}.h")
endforeach()

set(_brahma_include_candidates
  ${BRAHMA_INCLUDE_DIRS}
  "${CMAKE_BINARY_DIR}/src/brahma/include"
  "${CMAKE_INSTALL_PREFIX}/include"
)

set(_brahma_include_root "")
foreach(_candidate IN LISTS _brahma_include_candidates)
  set(_candidate_has_all_headers TRUE)
  foreach(_required_header IN LISTS _brahma_required_headers)
    if(NOT EXISTS "${_candidate}/${_required_header}")
      set(_candidate_has_all_headers FALSE)
      break()
    endif()
  endforeach()

  if(_candidate_has_all_headers)
    set(_brahma_include_root "${_candidate}")
    break()
  endif()
endforeach()

if("${_brahma_include_root}" STREQUAL "")
  message(FATAL_ERROR
    "Could not locate Brahma generated interface headers. Checked: ${_brahma_include_candidates}")
endif()

set(_generated_dir "${CMAKE_SOURCE_DIR}/src/dftracer/core/brahma")
set(_generated_files "")
foreach(_iface IN LISTS _requested_interfaces)
  list(APPEND _generated_files
    "${_generated_dir}/${_iface}.h"
    "${_generated_dir}/${_iface}.cpp")
endforeach()

set(_brahma_header_deps "")
foreach(_required_header IN LISTS _brahma_required_headers)
  list(APPEND _brahma_header_deps "${_brahma_include_root}/${_required_header}")
endforeach()

set(_generator_cmd
  ${_dftracer_generator_python}
  ${_generator_script}
  --workspace-root ${CMAKE_SOURCE_DIR}
  --brahma-include-root ${_brahma_include_root}
  --output-dir ${_generated_dir}
  --interfaces ${_requested_interfaces}
)

# Forward exact MPI/HDF5 include dirs that CMake's find_package located. The
# regen script's auto-detection (h5cc/mpicc -show) misses paths on some
# distros (Ubuntu mpich), and without the system headers libclang silently
# resolves typedefs like `hid_t` to `int`, producing overrides whose
# signatures don't match brahma's parent class.
set(_extra_include_dirs "")
if(DFTRACER_ENABLE_HDF5)
  # HDF5 is already located in the parent CMakeLists before this subdir is
  # added, so HDF5_INCLUDE_DIRS is populated.
  if(HDF5_INCLUDE_DIRS)
    list(APPEND _extra_include_dirs ${HDF5_INCLUDE_DIRS})
  endif()
endif()
if(DFTRACER_ENABLE_MPI)
  # MPI's find_package call lives later in the parent CMakeLists, so its
  # cache vars aren't yet populated when this subdir runs. Look it up here.
  find_package(MPI QUIET COMPONENTS C CXX)
  foreach(_mpi_var MPI_C_INCLUDE_DIRS MPI_CXX_INCLUDE_DIRS MPI_C_INCLUDE_PATH MPI_CXX_INCLUDE_PATH)
    if(${_mpi_var})
      list(APPEND _extra_include_dirs ${${_mpi_var}})
    endif()
  endforeach()
endif()
if(_extra_include_dirs)
  list(REMOVE_DUPLICATES _extra_include_dirs)
endif()
foreach(_inc IN LISTS _extra_include_dirs)
  list(APPEND _generator_cmd --extra-include-dir ${_inc})
endforeach()

if(DFTRACER_GENERATE_INTERFACES_VERBOSE)
  list(APPEND _generator_cmd --verbose)
endif()

add_custom_command(
  OUTPUT ${_generated_files}
  COMMAND ${_generator_cmd}
  DEPENDS
    ${_generator_script}
    ${_brahma_header_deps}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generating DFTracer interfaces from Brahma-generated headers"
  VERBATIM
)

add_custom_target(${PROJECT_NAME}_generate_interfaces DEPENDS ${_generated_files})

message(STATUS "[DFTRACER] Interface generation enabled")
message(STATUS "[DFTRACER] Interfaces: ${_requested_interfaces}")
message(STATUS "[DFTRACER] Brahma include root: ${_brahma_include_root}")
message(STATUS "[DFTRACER] Generator Python: ${_dftracer_generator_python}")
message(STATUS "[DFTRACER] Generator script: ${_generator_script}")
