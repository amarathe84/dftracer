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

# Pin the same libclang the rest of the build uses.
set(_dftracer_libclang_path "")
if(DEFINED ENV{LIBCLANG_PATH} AND EXISTS "$ENV{LIBCLANG_PATH}")
  set(_dftracer_libclang_path "$ENV{LIBCLANG_PATH}")
elseif(DEFINED LLVM_DIR)
  foreach(_libclang_candidate
      "${LLVM_DIR}/../../libclang.so"
      "${LLVM_DIR}/../../../lib/libclang.so")
    get_filename_component(_libclang_candidate "${_libclang_candidate}" REALPATH)
    if(EXISTS "${_libclang_candidate}")
      set(_dftracer_libclang_path "${_libclang_candidate}")
      break()
    endif()
  endforeach()
endif()
if(_dftracer_libclang_path)
  list(APPEND _generator_cmd --libclang-path ${_dftracer_libclang_path})
endif()

# Forward exact MPI/HDF5 include dirs that CMake's find_package located.
set(_extra_include_dirs "")

# Helper: extract -I include dirs from `<wrapper> -show` output.
function(_dftracer_collect_includes_from_wrapper _wrapper _out_var)
  find_program(_${_wrapper}_exe NAMES ${_wrapper})
  if(NOT _${_wrapper}_exe)
    return()
  endif()
  execute_process(
    COMMAND ${_${_wrapper}_exe} -show
    OUTPUT_VARIABLE _wrapper_out
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _wrapper_out)
    return()
  endif()
  separate_arguments(_wrapper_tokens UNIX_COMMAND "${_wrapper_out}")
  set(_collected "")
  foreach(_tok IN LISTS _wrapper_tokens)
    if(_tok MATCHES "^-I(.+)$")
      list(APPEND _collected "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  set(${_out_var} ${${_out_var}} ${_collected} PARENT_SCOPE)
endfunction()

if(DFTRACER_ENABLE_HDF5)
  # 1. CMake's find_package(HDF5) result.
  if(HDF5_INCLUDE_DIRS)
    list(APPEND _extra_include_dirs ${HDF5_INCLUDE_DIRS})
  endif()
  # 2. Imported target if available; modern CMake's source of truth.
  foreach(_hdf5_target hdf5::hdf5 HDF5::HDF5 hdf5::hdf5_hl)
    if(TARGET ${_hdf5_target})
      get_target_property(_hdf5_iface_inc ${_hdf5_target} INTERFACE_INCLUDE_DIRECTORIES)
      if(_hdf5_iface_inc)
        list(APPEND _extra_include_dirs ${_hdf5_iface_inc})
      endif()
    endif()
  endforeach()
  # 3. Query the HDF5 compiler wrapper.
  foreach(_hdf5_wrapper h5pcc h5pcc.mpich h5pcc.openmpi h5cc)
    _dftracer_collect_includes_from_wrapper(${_hdf5_wrapper} _extra_include_dirs)
  endforeach()
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
  _dftracer_collect_includes_from_wrapper(mpicc _extra_include_dirs)
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
message(STATUS "[DFTRACER] HDF5_INCLUDE_DIRS: ${HDF5_INCLUDE_DIRS}")
message(STATUS "[DFTRACER] MPI_C_INCLUDE_DIRS: ${MPI_C_INCLUDE_DIRS}")
message(STATUS "[DFTRACER] Extra include dirs forwarded to regen: ${_extra_include_dirs}")
message(STATUS "[DFTRACER] libclang forwarded to regen: ${_dftracer_libclang_path}")
