#!/bin/bash
# Automated build script for DFTracer
# This script installs dependencies and builds DFTracer with configurable options

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${SCRIPT_DIR}/install}"
BUILD_TYPE="${DFTRACER_BUILD_TYPE:-Release}"
PYTHON_EXE="${PYTHON_EXE:-}"
USE_PYTHON="${USE_PYTHON:-auto}"
BUILD_DEPENDENCIES="${DFTRACER_BUILD_DEPENDENCIES:-1}"
ENABLE_TESTS="${DFTRACER_ENABLE_TESTS:-OFF}"
ENABLE_FTRACING="${DFTRACER_ENABLE_FTRACING:-OFF}"
ENABLE_HIP_TRACING="${DFTRACER_ENABLE_HIP_TRACING:-OFF}"
ENABLE_MPI="${DFTRACER_ENABLE_MPI:-OFF}"
ENABLE_HDF5="${DFTRACER_ENABLE_HDF5:-OFF}"
ENABLE_DYNAMIC_DETECTION="${DFTRACER_ENABLE_DYNAMIC_DETECTION:-OFF}"
GENERATE_INTERFACES="${DFTRACER_GENERATE_INTERFACES:-OFF}"
DISABLE_HWLOC="${DFTRACER_DISABLE_HWLOC:-ON}"
ENABLE_DLIO_TESTS="${DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS:-OFF}"
ENABLE_PAPER_TESTS="${DFTRACER_ENABLE_PAPER_TESTS:-OFF}"
CMAKE_ARGS="${DFTRACER_CMAKE_ARGS:-}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
CLEAN_BUILD="${CLEAN_BUILD:-0}"
CLEAN_INSTALL="${CLEAN_INSTALL:-0}"
INSTALL_MODE="${INSTALL_MODE:-pip}"  # pip or cmake
INSTALL_DFANALYZER="${INSTALL_DFANALYZER:-0}"  # Install dfanalyzer extras
DRY_RUN="${DRY_RUN:-0}"
VERBOSE="${VERBOSE:-0}"
ENABLE_COVERAGE="${ENABLE_COVERAGE:-0}"  # Build with coverage support
RUN_SMOKE_TEST="${RUN_SMOKE_TEST:-1}"

# Print usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Automated build script for DFTracer. Installs dependencies and builds DFTracer.

OPTIONS:
    -h, --help              Show this help message
    --build-dir DIR         Build directory (default: ./build)
    --install-prefix DIR    Install prefix (default: ./install)
    --build-type TYPE       Build type: Release, Debug, RelWithDebInfo, PROFILE (default: Release)
    --python PATH           Python executable path (enables Python support, default: none)
    --skip-deps             Skip building dependencies
    --enable-tests          Enable tests
    --enable-coverage       Enable coverage analysis (sets build type to PROFILE and enables tests)
    --enable-ftracing       Enable function tracing
    --enable-hip            Enable HIP tracing
    --enable-mpi            Enable MPI support
    --enable-hdf5           Enable HDF5 support
    --enable-dynamic-detection Enable dynamic detection of MPI, HWLOC, and HIP at runtime
    --generate-interfaces   Generate Brahma/DFTracer interfaces from discovered MPI/HDF5 headers
    --enable-hwloc          Enable HWLOC (default: disabled)
    --enable-dlio-tests     Enable DLIO benchmark tests
    --enable-paper-tests    Enable paper tests
    --jobs N                Number of parallel jobs (default: auto-detected)
    --clean                 Clean build directory before building
    --clean-install         Remove all DFTracer installations from system/venv (site-packages, bin, lib, lib64)
    --with-dfanalyzer       Install dfanalyzer dependencies (for analysis tools)
    --install-mode MODE     Installation mode: pip or cmake (default: pip)
    --dry-run               Show what would be done without executing
    --verbose, -v           Enable verbose output
    --skip-smoke-test       Skip post-build dftracer_service start/stop smoke test

ENVIRONMENT VARIABLES (same as setup.py):
    DFTRACER_BUILD_TYPE                     Build type (Release/Debug)
    DFTRACER_BUILD_DEPENDENCIES             Build dependencies (1/0, default: 1)
    DFTRACER_ENABLE_TESTS                   Enable tests (ON/OFF)
    DFTRACER_ENABLE_FTRACING                Enable function tracing (ON/OFF)
    DFTRACER_ENABLE_HIP_TRACING             Enable HIP tracing (ON/OFF)
    DFTRACER_ENABLE_MPI                     Enable MPI (ON/OFF)
    DFTRACER_ENABLE_HDF5                    Enable HDF5 (ON/OFF)
    DFTRACER_ENABLE_DYNAMIC_DETECTION       Enable dynamic detection (ON/OFF)
    DFTRACER_GENERATE_INTERFACES            Generate interfaces from system headers (ON/OFF)
    DFTRACER_DISABLE_HWLOC                  Disable HWLOC (ON/OFF)
    DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS    Enable DLIO tests (ON/OFF)
    DFTRACER_ENABLE_PAPER_TESTS             Enable paper tests (ON/OFF)
    DFTRACER_CMAKE_ARGS                     Additional CMake arguments (semicolon-separated)
    DFTRACER_INSTALL_DIR                    Installation directory
    DFTRACER_PYTHON_SITE                    Python site-packages directory

EXAMPLES:
    # Basic build with pip installation (recommended)
    $0

    # Build with tests enabled
    $0 --enable-tests

    # Build with coverage analysis support
    $0 --enable-coverage

    # Clean build with custom install prefix
    $0 --clean --install-prefix /usr/local

    # Build with CMake installation
    $0 --install-mode cmake

    # Build with MPI support
    $0 --enable-mpi

    # Build with MPI + HDF5 and generated interfaces
    $0 --enable-mpi --enable-hdf5 --generate-interfaces

    # Build with dfanalyzer for analysis tools
    $0 --with-dfanalyzer

    # Debug build with all tests
    $0 --build-type Debug --enable-tests --enable-dlio-tests --enable-paper-tests

    # Clean all DFTracer installations
    $0 --clean-install

    # Dry run to see what would be executed
    $0 --dry-run --enable-tests

    # Verbose output for debugging
    $0 --verbose --enable-tests

COVERAGE ANALYSIS WORKFLOW:
    # 1. Build with coverage support
    $0 --enable-coverage

    # 2. Run tests and generate coverage report
    ./script/coverage_after_autobuild.sh

    # 3. Generate detailed report for analysis
    ./script/generate_coverage_report.sh > coverage_report.txt

    # 4. View HTML report
    open build/coverage/html/index.html

EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --install-prefix)
            INSTALL_PREFIX="$2"
            export DFTRACER_INSTALL_DIR="$2"
            shift 2
            ;;
        --build-type)
            BUILD_TYPE="$2"
            export DFTRACER_BUILD_TYPE="$2"
            shift 2
            ;;
        --python)
            PYTHON_EXE="$2"
            USE_PYTHON="yes"
            shift 2
            ;;
        --skip-deps)
            BUILD_DEPENDENCIES="0"
            export DFTRACER_BUILD_DEPENDENCIES="0"
            shift
            ;;
        --enable-tests)
            ENABLE_TESTS="ON"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --enable-coverage)
            ENABLE_COVERAGE="1"
            BUILD_TYPE="PROFILE"
            ENABLE_TESTS="ON"
            export DFTRACER_BUILD_TYPE="PROFILE"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --enable-ftracing)
            ENABLE_FTRACING="ON"
            export DFTRACER_ENABLE_FTRACING="ON"
            shift
            ;;
        --enable-hip)
            ENABLE_HIP_TRACING="ON"
            export DFTRACER_ENABLE_HIP_TRACING="ON"
            shift
            ;;
        --enable-mpi)
            ENABLE_MPI="ON"
            export DFTRACER_ENABLE_MPI="ON"
            shift
            ;;
        --enable-hdf5)
            ENABLE_HDF5="ON"
            export DFTRACER_ENABLE_HDF5="ON"
            shift
            ;;
        --enable-dynamic-detection)
            ENABLE_DYNAMIC_DETECTION="ON"
            export DFTRACER_ENABLE_DYNAMIC_DETECTION="ON"
            shift
            ;;
        --generate-interfaces)
            GENERATE_INTERFACES="ON"
            export DFTRACER_GENERATE_INTERFACES="ON"
            shift
            ;;
        --enable-hwloc)
            DISABLE_HWLOC="OFF"
            export DFTRACER_DISABLE_HWLOC="OFF"
            shift
            ;;
        --enable-dlio-tests)
            ENABLE_DLIO_TESTS="ON"
            export DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS="ON"
            shift
            ;;
        --enable-paper-tests)
            ENABLE_PAPER_TESTS="ON"
            export DFTRACER_ENABLE_PAPER_TESTS="ON"
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --clean)
            CLEAN_BUILD="1"
            shift
            ;;
        --clean-install)
            CLEAN_INSTALL="1"
            shift
            ;;
        --with-dfanalyzer)
            INSTALL_DFANALYZER="1"
            shift
            ;;
        --install-mode)
            INSTALL_MODE="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN="1"
            shift
            ;;
        --verbose|-v)
            VERBOSE="1"
            shift
            ;;
        --skip-smoke-test)
            RUN_SMOKE_TEST="0"
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

# Helper function for executing commands
execute_cmd() {
    local description="$1"
    shift
    
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${BLUE}[VERBOSE] ${description}${NC}"
        echo -e "${BLUE}[VERBOSE] Command: $*${NC}"
    fi
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: $*${NC}"
        return 0
    else
        "$@"
    fi
}

run_service_smoke_test() {
    if [ "${RUN_SMOKE_TEST}" != "1" ]; then
        echo -e "${YELLOW}Skipping smoke test (--skip-smoke-test)${NC}"
        return 0
    fi

    local service_bin=""
    if command -v dftracer_service &> /dev/null; then
        service_bin="$(command -v dftracer_service)"
    elif [ -x "${BUILD_DIR}/bin/dftracer_service" ]; then
        service_bin="${BUILD_DIR}/bin/dftracer_service"
    elif [ -x "${INSTALL_PREFIX}/bin/dftracer_service" ]; then
        service_bin="${INSTALL_PREFIX}/bin/dftracer_service"
    elif [ -n "${VIRTUAL_ENV}" ] && [ -x "${VIRTUAL_ENV}/bin/dftracer_service" ]; then
        service_bin="${VIRTUAL_ENV}/bin/dftracer_service"
    elif [ -n "${CONDA_PREFIX}" ] && [ -x "${CONDA_PREFIX}/bin/dftracer_service" ]; then
        service_bin="${CONDA_PREFIX}/bin/dftracer_service"
    fi

    if [ -z "${service_bin}" ]; then
        echo -e "${RED}Smoke test failed: could not find dftracer_service binary${NC}"
        return 1
    fi

    local smoke_dir="${BUILD_DIR}/smoke_service"
    local pid_file="${smoke_dir}/dftracer_server.pid"
    mkdir -p "${smoke_dir}"

    (
        export DFTRACER_ENABLE=1
        export DFTRACER_LOG_FILE="${smoke_dir}/trace"
        export DFTRACER_TRACE_INTERVAL_MS=100
        : "${DFTRACER_LIBUV_THREADS:=1}"
        export DFTRACER_LIBUV_THREADS

        echo -e "${BLUE}Running dftracer_service smoke test with ${service_bin}${NC}"
        if ! "${service_bin}" start "${smoke_dir}"; then
            echo -e "${RED}Smoke test failed: service start command failed${NC}"
            exit 1
        fi

        for _ in $(seq 1 20); do
            if [ -f "${pid_file}" ]; then
                break
            fi
            sleep 0.1
        done

        if [ ! -f "${pid_file}" ]; then
            echo -e "${RED}Smoke test failed: pid file was not created${NC}"
            exit 1
        fi

        if ! "${service_bin}" stop "${smoke_dir}"; then
            echo -e "${RED}Smoke test failed: service stop command failed${NC}"
            exit 1
        fi

        if [ -f "${pid_file}" ]; then
            echo -e "${RED}Smoke test failed: pid file still exists after stop${NC}"
            exit 1
        fi

        echo -e "${GREEN}Smoke test passed: dftracer_service started and stopped successfully${NC}"
        exit 0
    )
    return $?
}

# Auto-detect Python if not explicitly disabled
if [ "$USE_PYTHON" = "auto" ]; then
    # Try to find Python
    if command -v python3 &> /dev/null; then
        PYTHON_EXE=$(which python3)
        USE_PYTHON="yes"
    elif command -v python &> /dev/null; then
        PYTHON_EXE=$(which python)
        USE_PYTHON="yes"
    else
        USE_PYTHON="no"
    fi
fi

# Verify we're in a virtual environment if Python is enabled
if [ "$USE_PYTHON" = "yes" ]; then
    # Check if we're in a virtual environment (venv or conda)
    IN_VENV=0
    if [ -n "$VIRTUAL_ENV" ]; then
        IN_VENV=1
        echo -e "${GREEN}Detected Python venv: ${VIRTUAL_ENV}${NC}"
    elif [ -n "$CONDA_PREFIX" ]; then
        IN_VENV=1
        echo -e "${GREEN}Detected Conda environment: ${CONDA_PREFIX}${NC}"
    fi
    
    if [ $IN_VENV -eq 0 ]; then
        echo -e "${RED}Error: Python support requires an active virtual environment${NC}"
        echo "Please activate a virtual environment before running this script:"
        echo ""
        echo "  For venv:"
        echo "    python3 -m venv dftracer_env"
        echo "    source dftracer_env/bin/activate"
        echo ""
        echo "  For conda:"
        echo "    conda create -n dftracer python=3.10"
        echo "    conda activate dftracer"
        echo ""
        echo "Then run this script again with --python python3"
        exit 1
    fi
fi

# Handle --clean-install flag
if [ "$CLEAN_INSTALL" = "1" ]; then
    echo -e "${GREEN}=== Cleaning DFTracer Installation ===${NC}"
    echo ""
    
    # First, uninstall via pip if Python is available
    if [ "$USE_PYTHON" = "yes" ] || command -v python3 &> /dev/null; then
        PYTHON_FOR_CLEAN="${PYTHON_EXE:-python3}"
        
        if command -v "${PYTHON_FOR_CLEAN}" &> /dev/null; then
            echo "Checking for pip-installed DFTracer..."
            
            # Check if dftracer or pydftracer is installed
            if "${PYTHON_FOR_CLEAN}" -m pip show dftracer &> /dev/null || "${PYTHON_FOR_CLEAN}" -m pip show pydftracer &> /dev/null; then
                if [ "$DRY_RUN" = "1" ]; then
                    echo -e "${YELLOW}[DRY-RUN] Would uninstall dftracer/pydftracer via pip${NC}"
                else
                    echo "Uninstalling dftracer and pydftracer via pip..."
                    "${PYTHON_FOR_CLEAN}" -m pip uninstall -y dftracer pydftracer 2>/dev/null || true
                    echo -e "${GREEN}Pip packages uninstalled${NC}"
                fi
            else
                echo "No pip-installed dftracer found"
            fi
            echo ""
        fi
    fi
    
    # Determine what to clean
    CLEAN_LOCATIONS=()
    
    # Check for Python site-packages
    if [ "$USE_PYTHON" = "yes" ] || command -v python3 &> /dev/null; then
        PYTHON_FOR_CLEAN="${PYTHON_EXE:-python3}"
        
        if command -v "${PYTHON_FOR_CLEAN}" &> /dev/null; then
            SITE_PACKAGES=$("${PYTHON_FOR_CLEAN}" -c "import site; print(site.getsitepackages()[0])" 2>/dev/null || echo "")
            
            if [ -n "${SITE_PACKAGES}" ]; then
                # Check for dftracer in site-packages
                if [ -d "${SITE_PACKAGES}/dftracer" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/dftracer")
                fi
                if [ -d "${SITE_PACKAGES}/pydftracer.egg-info" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/pydftracer.egg-info")
                fi
                if [ -d "${SITE_PACKAGES}/dftracer.egg-info" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/dftracer.egg-info")
                fi
                # Check for egg-link files (editable installs)
                if [ -f "${SITE_PACKAGES}/dftracer.egg-link" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/dftracer.egg-link")
                fi
                if [ -f "${SITE_PACKAGES}/pydftracer.egg-link" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/pydftracer.egg-link")
                fi
                # Check for .pth files
                for pth_file in "${SITE_PACKAGES}"/__editable__.dftracer*.pth "${SITE_PACKAGES}"/__editable__.pydftracer*.pth; do
                    if [ -f "$pth_file" ]; then
                        CLEAN_LOCATIONS+=("$pth_file")
                    fi
                done
                # Check for .so files
                for so_file in "${SITE_PACKAGES}"/dftracer*.so; do
                    if [ -f "$so_file" ]; then
                        CLEAN_LOCATIONS+=("$so_file")
                    fi
                done
            fi
        fi
    fi
    
    # Check install prefix
    if [ -d "${INSTALL_PREFIX}" ]; then
        # Check bin directory
        if [ -d "${INSTALL_PREFIX}/bin" ]; then
            for file in "${INSTALL_PREFIX}/bin/"dftracer*; do
                if [ -e "$file" ]; then
                    CLEAN_LOCATIONS+=("$file")
                fi
            done
        fi
        
        # Check lib directory
        for libdir in lib lib64; do
            if [ -d "${INSTALL_PREFIX}/${libdir}" ]; then
                for file in "${INSTALL_PREFIX}/${libdir}/"*dftracer*; do
                    if [ -e "$file" ]; then
                        CLEAN_LOCATIONS+=("$file")
                    fi
                done
            fi
        done
        
        # Check include directory
        if [ -d "${INSTALL_PREFIX}/include/dftracer" ]; then
            CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/include/dftracer")
        fi
        
        # Check share directory
        if [ -d "${INSTALL_PREFIX}/share/dftracer" ]; then
            CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/share/dftracer")
        fi
        
        # Check for DFTracer dependencies (cpp-logger, gotcha, brahma, yaml-cpp)
        for dep in cpp-logger cpp_logger gotcha brahma yaml-cpp; do
            # Check lib directories
            for libdir in lib lib64; do
                if [ -d "${INSTALL_PREFIX}/${libdir}" ]; then
                    # Check for libraries
                    for file in "${INSTALL_PREFIX}/${libdir}/"*${dep}* "${INSTALL_PREFIX}/${libdir}/cmake/${dep}"*; do
                        if [ -e "$file" ]; then
                            CLEAN_LOCATIONS+=("$file")
                        fi
                    done
                fi
            done
            
            # Check include directories
            if [ -d "${INSTALL_PREFIX}/include/${dep}" ]; then
                CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/include/${dep}")
            fi
            
            # Check share/cmake directories
            if [ -d "${INSTALL_PREFIX}/share/${dep}" ]; then
                CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/share/${dep}")
            fi
        done
    fi
    
    # Display what will be cleaned
    if [ ${#CLEAN_LOCATIONS[@]} -eq 0 ]; then
        echo -e "${YELLOW}No DFTracer installations found to clean.${NC}"
    else
        echo -e "${YELLOW}The following locations will be removed:${NC}"
        for location in "${CLEAN_LOCATIONS[@]}"; do
            echo "  - $location"
        done
        echo ""
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would remove the above locations${NC}"
        else
            read -p "Are you sure you want to remove these? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                for location in "${CLEAN_LOCATIONS[@]}"; do
                    if [ -e "$location" ]; then
                        echo "Removing: $location"
                        rm -rf "$location"
                    fi
                done
                echo -e "${GREEN}Cleanup complete!${NC}"
            else
                echo -e "${YELLOW}Cleanup cancelled.${NC}"
                exit 0
            fi
        fi
    fi
    
    # If only clean-install was requested, exit now
    if [ "$CLEAN_BUILD" = "0" ] && [ "$BUILD_DEPENDENCIES" = "0" ]; then
        exit 0
    fi
    echo ""
fi

# Print configuration
echo -e "${GREEN}=== DFTracer Auto-Build Configuration ===${NC}"
if [ "$DRY_RUN" = "1" ]; then
    echo -e "${YELLOW}*** DRY RUN MODE - No actual changes will be made ***${NC}"
fi
if [ "$VERBOSE" = "1" ]; then
    echo -e "${BLUE}*** VERBOSE MODE - Detailed output enabled ***${NC}"
fi
echo "Build Directory: ${BUILD_DIR}"
echo "Install Prefix: ${INSTALL_PREFIX}"
echo "Build Type: ${BUILD_TYPE}"
if [ "$USE_PYTHON" = "yes" ]; then
    echo "Python Support: Enabled"
    echo "Python Executable: ${PYTHON_EXE}"
else
    echo "Python Support: Disabled"
fi
echo "Build Dependencies: ${BUILD_DEPENDENCIES}"
echo "Enable Tests: ${ENABLE_TESTS}"
echo "Enable Function Tracing: ${ENABLE_FTRACING}"
echo "Enable HIP Tracing: ${ENABLE_HIP_TRACING}"
echo "Enable MPI: ${ENABLE_MPI}"
echo "Enable HDF5: ${ENABLE_HDF5}"
echo "Generate Interfaces: ${GENERATE_INTERFACES}"
echo "Disable HWLOC: ${DISABLE_HWLOC}"
echo "Enable DLIO Tests: ${ENABLE_DLIO_TESTS}"
echo "Enable Paper Tests: ${ENABLE_PAPER_TESTS}"
echo "Parallel Jobs: ${JOBS}"
echo "Install Mode: ${INSTALL_MODE}"
echo "Install DFAnalyzer: ${INSTALL_DFANALYZER}"
echo "Run Smoke Test: ${RUN_SMOKE_TEST}"
echo "Dry Run: ${DRY_RUN}"
echo "Verbose: ${VERBOSE}"
echo ""

# Verify Python if enabled
if [ "$USE_PYTHON" = "yes" ]; then
    if ! command -v "${PYTHON_EXE}" &> /dev/null; then
        echo -e "${RED}Error: Python executable not found: ${PYTHON_EXE}${NC}"
        echo "Please install Python 3 or specify a valid path with --python /path/to/python3"
        exit 1
    fi

    PYTHON_VERSION=$("${PYTHON_EXE}" --version 2>&1 | awk '{print $2}' || echo "unknown")
    if [ -n "${PYTHON_VERSION}" ] && [ "${PYTHON_VERSION}" != "unknown" ]; then
        echo -e "${GREEN}Using Python: ${PYTHON_VERSION}${NC}"
    else
        echo -e "${YELLOW}Warning: Could not determine Python version${NC}"
    fi
else
    echo -e "${YELLOW}Building without Python support${NC}"
fi
echo ""

# Clean build directory if requested
if [ "$CLEAN_BUILD" = "1" ] && [ -d "${BUILD_DIR}" ]; then
    echo -e "${YELLOW}Cleaning build directory: ${BUILD_DIR}${NC}"
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would remove: ${BUILD_DIR}${NC}"
    else
        rm -rf "${BUILD_DIR}"
    fi
fi

# Create build directory
if [ "$DRY_RUN" = "1" ]; then
    echo -e "${YELLOW}[DRY-RUN] Would create directory: ${BUILD_DIR}${NC}"
else
    mkdir -p "${BUILD_DIR}"
fi

# Export environment variables
export DFTRACER_BUILD_TYPE="${BUILD_TYPE}"
export DFTRACER_BUILD_DEPENDENCIES="${BUILD_DEPENDENCIES}"
export DFTRACER_ENABLE_TESTS="${ENABLE_TESTS}"
export DFTRACER_ENABLE_FTRACING="${ENABLE_FTRACING}"
export DFTRACER_ENABLE_HIP_TRACING="${ENABLE_HIP_TRACING}"
export DFTRACER_ENABLE_MPI="${ENABLE_MPI}"
export DFTRACER_ENABLE_HDF5="${ENABLE_HDF5}"
export DFTRACER_DISABLE_HWLOC="${DISABLE_HWLOC}"
export DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS="${ENABLE_DLIO_TESTS}"
export DFTRACER_ENABLE_PAPER_TESTS="${ENABLE_PAPER_TESTS}"
export DFTRACER_GENERATE_INTERFACES="${GENERATE_INTERFACES}"

if [ -n "${INSTALL_PREFIX}" ]; then
    export DFTRACER_INSTALL_DIR="${INSTALL_PREFIX}"
fi

if [ "$INSTALL_MODE" = "pip" ]; then
    if [ "$USE_PYTHON" != "yes" ]; then
        echo -e "${RED}Error: pip install mode requires Python${NC}"
        echo "Either specify --python /path/to/python3 or use --install-mode cmake"
        exit 1
    fi
    
    echo -e "${GREEN}=== Building and Installing DFTracer with pip ===${NC}"
    echo "This will install dependencies, build, and install DFTracer in the active virtual environment"
    echo ""
    
    # First, ensure build dependencies are installed
    echo -e "${GREEN}Step 0: Installing Python build dependencies${NC}"
    echo ""
    
    # Upgrade pip first to ensure we have the latest version
    "${PYTHON_EXE}" -m pip install --upgrade pip
    
    # Install build dependencies with normal isolation (not using --no-build-isolation here)
    BUILD_DEPS_CMD=("${PYTHON_EXE}" -m pip install --upgrade setuptools wheel setuptools-scm pybind11 scikit-build-core cmake ninja clang)
    
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${BLUE}[VERBOSE] Build dependencies command: ${BUILD_DEPS_CMD[*]}${NC}"
    fi
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${BUILD_DEPS_CMD[*]}${NC}"
    else
        if ! "${BUILD_DEPS_CMD[@]}"; then
            echo -e "${RED}Failed to install Python build dependencies${NC}"
            exit 1
        fi
        echo -e "${GREEN}Python build dependencies installed successfully${NC}"
        # Install gcovr if coverage is enabled
        if [ "$ENABLE_COVERAGE" = "1" ]; then
            echo -e "${GREEN}Installing gcovr for coverage analysis...${NC}"
            if ! "${PYTHON_EXE}" -m pip install gcovr; then
                echo -e "${YELLOW}Warning: Failed to install gcovr. Coverage analysis may not work.${NC}"
            else
                echo -e "${GREEN}gcovr installed successfully${NC}"
            fi
        fi
    fi
    echo ""
    
    # If dependencies should be built, do it first
    if [ "$BUILD_DEPENDENCIES" = "1" ]; then
        echo -e "${GREEN}Step 1: Building C++ dependencies and DFTracer${NC}"
        echo ""
        
        # Set environment to build dependencies
        export DFTRACER_BUILD_DEPENDENCIES="1"
        
        # Build pip extras based on flags
        PIP_EXTRAS="test"
        if [ "$INSTALL_DFANALYZER" = "1" ]; then
            PIP_EXTRAS="test,dfanalyzer"
        fi
        
        # Do a full build which includes dependencies
        FULL_BUILD_CMD=("${PYTHON_EXE}" -m pip install --no-cache-dir ".[${PIP_EXTRAS}]")

        if [ "${DFTRACER_PIP_NO_BUILD_ISOLATION:-0}" = "1" ]; then
            FULL_BUILD_CMD+=(--no-build-isolation)
        fi

        if [ "$VERBOSE" = "1" ]; then
            FULL_BUILD_CMD+=(-v)
            echo -e "${BLUE}[VERBOSE] Full build command: ${FULL_BUILD_CMD[*]}${NC}"
        fi
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: ${FULL_BUILD_CMD[*]}${NC}"
        else
            if ! "${FULL_BUILD_CMD[@]}"; then
                echo -e "${RED}Failed to build DFTracer with dependencies${NC}"
                exit 1
            fi
            echo -e "${GREEN}DFTracer and dependencies built successfully${NC}"
        fi
        echo ""
    else
        # Skip dependency build
        echo -e "${GREEN}Building DFTracer (skipping dependencies)${NC}"
        echo ""
        
        # Set environment to skip dependency build
        export DFTRACER_BUILD_DEPENDENCIES="0"
        
        # Set environment variables to avoid file locking issues
        
        # Build pip extras based on flags
        PIP_EXTRAS="test"
        if [ "$INSTALL_DFANALYZER" = "1" ]; then
            PIP_EXTRAS="test,dfanalyzer"
        fi
        
        # Build and install with pip (will use the virtual environment)
        PIP_CMD=("${PYTHON_EXE}" -m pip install --no-cache-dir ".[${PIP_EXTRAS}]")

        if [ "${DFTRACER_PIP_NO_BUILD_ISOLATION:-0}" = "1" ]; then
            PIP_CMD+=(--no-build-isolation)
        fi

        if [ "$VERBOSE" = "1" ]; then
            PIP_CMD+=(-v)
        fi
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: ${PIP_CMD[*]}${NC}"
        else
            if ! "${PIP_CMD[@]}"; then
                echo -e "${RED}Failed to build DFTracer${NC}"
                exit 1
            fi
            echo -e "${GREEN}DFTracer built successfully${NC}"
            # Install Python test requirements if tests are enabled
            if [ "$ENABLE_TESTS" = "ON" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
                echo "Installing Python test requirements..."
                if [ "$DRY_RUN" = "1" ]; then
                    echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install -r ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                else
                    if [ "$VERBOSE" = "1" ]; then
                        echo -e "${BLUE}[VERBOSE] Installing from: ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                    fi
                    if "${PYTHON_EXE}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
                        echo -e "${GREEN}✓ Python test requirements installed${NC}"
                    else
                        echo -e "${YELLOW}Warning: Failed to install Python test requirements${NC}"
                        echo "Some tests may fail. Install manually with:"
                        echo "  ${PYTHON_EXE} -m pip install -r test/py/requirements.txt"
                    fi
                fi
            fi
        fi
        echo ""
    fi
    
    # Print success message
    if [ "$DRY_RUN" = "0" ]; then
        if ! run_service_smoke_test; then
            exit 1
        fi

        echo ""
        echo -e "${GREEN}=== Build and Installation Successful ===${NC}"
        echo ""
        echo "DFTracer has been installed in your virtual environment."
        echo ""
        
        # Determine where to put the environment script
        if [ -n "$VIRTUAL_ENV" ]; then
            ENV_SCRIPT="${VIRTUAL_ENV}/bin/dftracer_env.sh"
        elif [ -n "$CONDA_PREFIX" ]; then
            ENV_SCRIPT="${CONDA_PREFIX}/bin/dftracer_env.sh"
        else
            ENV_SCRIPT="${SCRIPT_DIR}/dftracer_env.sh"
        fi
        
        # Create environment setup script
        cat > "${ENV_SCRIPT}" << EOF
#!/bin/bash
# DFTracer Environment Setup
# Source this file to set up your environment for DFTracer

# Add Python modules to PYTHONPATH for editable install
export PYTHONPATH="${SCRIPT_DIR}/python:\${PYTHONPATH}"

# Add dfanalyzer_old to PYTHONPATH
export PYTHONPATH="${SCRIPT_DIR}/dfanalyzer_old:\${PYTHONPATH}"

echo "DFTracer environment configured."
echo "Python modules: ${SCRIPT_DIR}/python"
EOF
        chmod +x "${ENV_SCRIPT}"
        
        echo "Environment setup script created: ${ENV_SCRIPT}"
        echo ""
        echo "To configure your environment, run:"
        echo "  source ${ENV_SCRIPT}"
        echo ""
        echo "Or add to your shell profile (~/.bashrc or ~/.zshrc):"
        echo "  source ${ENV_SCRIPT}"
        echo ""
        echo "To verify the installation:"
        echo "  source ${ENV_SCRIPT}"
        echo "  ${PYTHON_EXE} -c 'import dftracer; print(dftracer.__version__)'"
        echo ""
        echo "To run tests:"
        echo "  pytest test/"
        echo ""
    else
        echo ""
        echo -e "${GREEN}=== Dry Run Complete ===${NC}"
        echo "No changes were made. Remove --dry-run to execute."
        echo ""
    fi
else
    echo -e "${GREEN}=== Building DFTracer with CMake ===${NC}"
    echo ""
    
    # Prepare CMake arguments
    CMAKE_FULL_ARGS=(
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}"
        "-DDFTRACER_ENABLE_FTRACING=${ENABLE_FTRACING}"
        "-DDFTRACER_ENABLE_HIP_TRACING=${ENABLE_HIP_TRACING}"
        "-DDFTRACER_ENABLE_MPI=${ENABLE_MPI}"
        "-DDFTRACER_ENABLE_HDF5=${ENABLE_HDF5}"
        "-DDFTRACER_GENERATE_INTERFACES=${GENERATE_INTERFACES}"
        "-DDFTRACER_DISABLE_HWLOC=${DISABLE_HWLOC}"
        "-DDFTRACER_ENABLE_TESTS=${ENABLE_TESTS}"
        "-DDFTRACER_ENABLE_DLIO_BENCHMARK_TESTS=${ENABLE_DLIO_TESTS}"
        "-DDFTRACER_ENABLE_PAPER_TESTS=${ENABLE_PAPER_TESTS}"
    )
    
    # Add Python support if enabled
    if [ "$USE_PYTHON" = "yes" ]; then
        # Get pybind11 directory
        if [ "$DRY_RUN" = "1" ]; then
            PYBIND11_DIR="/path/to/pybind11"
            PYTHON_SITE_PACKAGES="/path/to/site-packages"
            echo -e "${YELLOW}[DRY-RUN] Would check for pybind11${NC}"
        else
            PYBIND11_DIR=$("${PYTHON_EXE}" -c "import pybind11; print(pybind11.get_cmake_dir())" 2>/dev/null || echo "")
            
            if [ -z "${PYBIND11_DIR}" ]; then
                echo -e "${YELLOW}pybind11 not found, installing in virtual environment...${NC}"
                "${PYTHON_EXE}" -m pip install --no-cache-dir pybind11
                PYBIND11_DIR=$("${PYTHON_EXE}" -c "import pybind11; print(pybind11.get_cmake_dir())")
            fi
            
            # Get Python site-packages directory
            PYTHON_SITE_PACKAGES=$("${PYTHON_EXE}" -c "import site; print(site.getsitepackages()[0])" 2>/dev/null || echo "")
            if [ -z "${PYTHON_SITE_PACKAGES}" ]; then
                echo -e "${YELLOW}Warning: Could not determine Python site-packages directory${NC}"
                PYTHON_SITE_PACKAGES=$("${PYTHON_EXE}" -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())" 2>/dev/null || echo "")
            fi
        fi
        
        if [ "$VERBOSE" = "1" ]; then
            echo -e "${BLUE}[VERBOSE] pybind11 directory: ${PYBIND11_DIR}${NC}"
            echo -e "${BLUE}[VERBOSE] Python site-packages: ${PYTHON_SITE_PACKAGES}${NC}"
        fi
        
        # Add Python-specific CMake arguments
        CMAKE_FULL_ARGS+=(
            "-DDFTRACER_PYTHON_EXE=${PYTHON_EXE}"
            "-DDFTRACER_PYTHON_SITE=${PYTHON_SITE_PACKAGES}"
            "-Dpybind11_DIR=${PYBIND11_DIR}"
            "-DDFTRACER_BUILD_PYTHON_BINDINGS=ON"
            "-DPYBIND11_FINDPYTHON=ON"
        )
    else
        # Disable Python bindings
        CMAKE_FULL_ARGS+=(
            "-DDFTRACER_BUILD_PYTHON_BINDINGS=OFF"
        )
    fi
    
    # Add custom CMake arguments
    if [ -n "${CMAKE_ARGS}" ]; then
        IFS=';' read -ra EXTRA_ARGS <<< "${CMAKE_ARGS}"
        for arg in "${EXTRA_ARGS[@]}"; do
            if [ -n "$arg" ]; then
                CMAKE_FULL_ARGS+=("$arg")
            fi
        done
    fi
    
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${BLUE}[VERBOSE] CMake Arguments:${NC}"
        for arg in "${CMAKE_FULL_ARGS[@]}"; do
            echo -e "${BLUE}[VERBOSE]   $arg${NC}"
        done
    fi
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would change to directory: ${BUILD_DIR}${NC}"
    else
        cd "${BUILD_DIR}"
    fi
    
    # Step 1: Install dependencies if requested
    if [ "$BUILD_DEPENDENCIES" = "1" ]; then
        echo -e "${BLUE}Step 1: Installing dependencies...${NC}"
        DEP_CMAKE_ARGS=("${CMAKE_FULL_ARGS[@]}")
        DEP_CMAKE_ARGS+=("-DDFTRACER_INSTALL_DEPENDENCIES=ON")
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: cmake ${SCRIPT_DIR} ${DEP_CMAKE_ARGS[*]}${NC}"
            echo -e "${YELLOW}[DRY-RUN] Would execute: cmake --build . -j${JOBS}${NC}"
            echo -e "${GREEN}Dependencies would be installed${NC}"
        else
            if [ "$VERBOSE" = "1" ]; then
                echo -e "${BLUE}[VERBOSE] Configuring dependencies with CMake${NC}"
            fi
            
            if cmake "${SCRIPT_DIR}" "${DEP_CMAKE_ARGS[@]}"; then
                if [ "$VERBOSE" = "1" ]; then
                    echo -e "${BLUE}[VERBOSE] Building dependencies${NC}"
                fi
                
                if cmake --build . -j"${JOBS}"; then
                    echo -e "${GREEN}Dependencies installed successfully${NC}"
                else
                    echo -e "${RED}Failed to build dependencies${NC}"
                    exit 1
                fi
            else
                echo -e "${RED}Failed to configure dependencies${NC}"
                exit 1
            fi
        fi
        echo ""
    fi
    
    # Step 2: Configure DFTracer
    echo -e "${BLUE}Step 2: Configuring DFTracer...${NC}"
    CMAKE_FULL_ARGS+=("-DDFTRACER_INSTALL_DEPENDENCIES=OFF")
    CMAKE_FULL_ARGS+=("-Dyaml-cpp_DIR=${INSTALL_PREFIX}")
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: cmake ${SCRIPT_DIR} ${CMAKE_FULL_ARGS[*]}${NC}"
    else
        if [ "$VERBOSE" = "1" ]; then
            echo -e "${BLUE}[VERBOSE] Final CMake configuration:${NC}"
            for arg in "${CMAKE_FULL_ARGS[@]}"; do
                echo -e "${BLUE}[VERBOSE]   $arg${NC}"
            done
        fi
        
        if ! cmake "${SCRIPT_DIR}" "${CMAKE_FULL_ARGS[@]}"; then
            echo -e "${RED}Failed to configure DFTracer${NC}"
            exit 1
        fi
    fi
    echo ""
    
    # Step 3: Build DFTracer
    echo -e "${BLUE}Step 3: Building DFTracer...${NC}"
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: cmake --build . -j${JOBS}${NC}"
    else
        if [ "$VERBOSE" = "1" ]; then
            echo -e "${BLUE}[VERBOSE] Building with ${JOBS} parallel jobs${NC}"
        fi
        
        if ! cmake --build . -j"${JOBS}"; then
            echo -e "${RED}Failed to build DFTracer${NC}"
            exit 1
        fi
        # Install Python test requirements if tests are enabled
        if [ "$ENABLE_TESTS" = "ON" ] && [ "$USE_PYTHON" = "yes" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
            echo "Installing Python test requirements..."
            if [ "$DRY_RUN" = "1" ]; then
                echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install -r ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
            else
                if [ "$VERBOSE" = "1" ]; then
                    echo -e "${BLUE}[VERBOSE] Installing from: ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                fi
                if "${PYTHON_EXE}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
                    echo -e "${GREEN}✓ Python test requirements installed${NC}"
                else
                    echo -e "${YELLOW}Warning: Failed to install Python test requirements${NC}"
                    echo "Some tests may fail. Install manually with:"
                    echo "  ${PYTHON_EXE} -m pip install -r test/py/requirements.txt"
                fi
            fi
        fi
    fi
    echo ""
    
    # Step 3.5: Install test dependencies if tests are enabled
    if [ "$ENABLE_TESTS" = "ON" ]; then
        echo -e "${BLUE}Step 3.5: Installing test dependencies...${NC}"
        
        # Check for jq (needed for coverage and test analysis)
        if ! command -v jq &> /dev/null; then
            echo -e "${YELLOW}Warning: jq not found. Attempting to install...${NC}"
            
            if [ "$DRY_RUN" = "1" ]; then
                echo -e "${YELLOW}[DRY-RUN] Would install jq${NC}"
            else
                # Try to detect package manager and install jq
                if command -v apt-get &> /dev/null; then
                    echo "Detected apt-get, installing jq..."
                    sudo apt-get update && sudo apt-get install -y jq || echo -e "${YELLOW}Could not install jq with apt-get${NC}"
                elif command -v yum &> /dev/null; then
                    echo "Detected yum, installing jq..."
                    sudo yum install -y jq || echo -e "${YELLOW}Could not install jq with yum${NC}"
                elif command -v brew &> /dev/null; then
                    echo "Detected brew, installing jq..."
                    brew install jq || echo -e "${YELLOW}Could not install jq with brew${NC}"
                else
                    echo -e "${YELLOW}Could not detect package manager. Please install jq manually:${NC}"
                    echo "  - Ubuntu/Debian: sudo apt-get install jq"
                    echo "  - RHEL/CentOS: sudo yum install jq"
                    echo "  - macOS: brew install jq"
                fi
            fi
        else
            echo -e "${GREEN}✓ jq is already installed${NC}"
        fi
        
        # Install Python test requirements if Python is enabled
        if [ "$USE_PYTHON" = "yes" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
            echo "Installing Python test requirements..."
            
            if [ "$DRY_RUN" = "1" ]; then
                echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install -r test/py/requirements.txt${NC}"
            else
                if [ "$VERBOSE" = "1" ]; then
                    echo -e "${BLUE}[VERBOSE] Installing from: ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                fi
                
                if "${PYTHON_EXE}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
                    echo -e "${GREEN}✓ Python test requirements installed${NC}"
                else
                    echo -e "${YELLOW}Warning: Failed to install Python test requirements${NC}"
                    echo "Some tests may fail. Install manually with:"
                    echo "  ${PYTHON_EXE} -m pip install -r test/py/requirements.txt"
                fi
            fi
        else
            if [ "$USE_PYTHON" != "yes" ]; then
                echo "Skipping Python test requirements (Python not enabled)"
            elif [ ! -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
                echo -e "${YELLOW}Note: test/py/requirements.txt not found${NC}"
            fi
        fi
        
        echo ""
    fi
    
    # Step 4: Install DFTracer
    echo -e "${BLUE}Step 4: Installing DFTracer...${NC}"
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: cmake --install .${NC}"
        echo ""
        echo -e "${GREEN}=== Dry Run Complete ===${NC}"
        echo ""
        echo "Would install to: ${INSTALL_PREFIX}"
        echo ""
        echo "No changes were made. Remove --dry-run to execute."
    else
        if cmake --install .; then
            if ! run_service_smoke_test; then
                exit 1
            fi

            echo ""
            echo -e "${GREEN}=== Build and Installation Successful ===${NC}"
            echo ""
            echo "Installation directory: ${INSTALL_PREFIX}"
            echo ""
            
            # Create environment setup script for CMake install
            ENV_SCRIPT="${INSTALL_PREFIX}/dftracer_env.sh"
            cat > "${ENV_SCRIPT}" << EOF
#!/bin/bash
# DFTracer Environment Setup
# Source this file to set up your environment for DFTracer

# Add DFTracer binaries to PATH
export PATH="${INSTALL_PREFIX}/bin:\${PATH}"

# Add DFTracer libraries to LD_LIBRARY_PATH
export LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX}/lib64:\${LD_LIBRARY_PATH}"

# Add DFTracer to PYTHONPATH (if Python bindings were built)
export PYTHONPATH="${INSTALL_PREFIX}:\${PYTHONPATH}"

echo "DFTracer environment configured."
echo "Install prefix: ${INSTALL_PREFIX}"
EOF
            chmod +x "${ENV_SCRIPT}"
            
            echo "Environment setup script created: ${ENV_SCRIPT}"
            echo ""
            echo "To configure your environment, run:"
            echo "  source ${ENV_SCRIPT}"
            echo ""
            echo "Or add to your shell profile (~/.bashrc or ~/.zshrc):"
            echo "  source ${ENV_SCRIPT}"
            echo ""
            if [ "$ENABLE_TESTS" = "ON" ]; then
                echo "To run tests:"
                echo "  cd ${BUILD_DIR} && ctest"
                echo ""
            fi
            if [ "$ENABLE_COVERAGE" = "1" ] || [ "$BUILD_TYPE" = "PROFILE" ]; then
                echo -e "${BLUE}=== Coverage Analysis ===${NC}"
                echo ""
                echo "Build is configured for coverage analysis."
                echo ""
                echo "To generate coverage report:"
                echo "  ./script/coverage_after_autobuild.sh"
                echo ""
                echo "To generate detailed analysis for test improvement:"
                echo "  ./script/generate_coverage_report.sh > coverage_report.txt"
                echo ""
                echo "View HTML report:"
                echo "  open ${BUILD_DIR}/coverage/html/index.html"
                echo ""
            fi
            echo ""
        else
            echo -e "${RED}Failed to install DFTracer${NC}"
            exit 1
        fi
    fi
fi
