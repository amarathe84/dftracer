#!/bin/bash
#
# Build and test DFTracer with HIP tracing enabled for AMD MI300A
# Runs on flux-managed pdebug queue
#
#SBATCH --job-name=dftracer-hip-build
#SBATCH --partition=pdebug
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --time=02:00:00
#SBATCH --output=/p/vast1/marathe1/dldl/dftrcer-omnistat/build-%j.out
#SBATCH --error=/p/vast1/marathe1/dldl/dftrcer-omnistat/build-%j.err

set -euo pipefail

# ------------------------------------------------------------------
# Environment setup
# ------------------------------------------------------------------
export HOME="${HOME:-/home/marathe1}"
export WORKSPACE="/usr/WS2/marathe1/dldl/dftracer-omnistat"
export BUILD_DIR="${WORKSPACE}/build-omnistat-hip"
export INSTALL_PREFIX="${WORKSPACE}/.local-hip"
export OUTPUT_DIR="/p/vast1/marathe1/dldl/dftrcer-omnistat"

echo "=== DFTracer HIP Build Job ==="
echo "WORKSPACE: ${WORKSPACE}"
echo "BUILD_DIR: ${BUILD_DIR}"
echo "INSTALL_PREFIX: ${INSTALL_PREFIX}"
echo "OUTPUT_DIR: ${OUTPUT_DIR}"

# ------------------------------------------------------------------
# Load modules needed for HIP/ROCm build
# ------------------------------------------------------------------
module purge
module load StdEnv
module load amd/6.4.3
module load cmake/3.24.2
module load cray-python/3.12.12

echo "=== Loaded modules ==="
module list

# ------------------------------------------------------------------
# Create build directory
# ------------------------------------------------------------------
mkdir -p "${BUILD_DIR}"

# ------------------------------------------------------------------
# Configure CMake with HIP tracing enabled
# ------------------------------------------------------------------
echo "=== Running CMake configure ==="
cmake -S "${WORKSPACE}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDFTRACER_ENABLE_HIP_TRACING=ON \
    -DDFTRACER_ENABLE_FTRACING=OFF \
    -DDFTRACER_ENABLE_MPI=OFF \
    -DDFTRACER_ENABLE_HDF5=OFF \
    -DDFTRACER_BUILD_PYTHON_BINDINGS=ON \
    -DDFTRACER_ENABLE_TESTS=OFF \
    -DDFTRACER_INSTALL_DEPENDENCIES=OFF \
    -DDFTRACER_DISABLE_HWLOC=ON \
    -DPYTHON_EXECUTABLE=$(which python3) \
    -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}"

# ------------------------------------------------------------------
# Build
# ------------------------------------------------------------------
echo "=== Building DFTracer ==="
make -C "${BUILD_DIR}" -j$(nproc) 2>&1 | tee "${BUILD_DIR}/build.log"

# ------------------------------------------------------------------
# Install
# ------------------------------------------------------------------
echo "=== Installing DFTracer ==="
make -C "${BUILD_DIR}" install 2>&1 | tee "${BUILD_DIR}/install.log"

# ------------------------------------------------------------------
# Verify build output
# ------------------------------------------------------------------
echo "=== Verifying build ==="
echo "Checking for built libraries..."
find "${BUILD_DIR}" -name "*.so" -o -name "*.a" 2>/dev/null | head -20
echo ""
echo "Checking installed libraries..."
find "${INSTALL_PREFIX}" -name "*.so" -o -name "*.a" 2>/dev/null | head -20

# ------------------------------------------------------------------
# Create test trace output directory
# ------------------------------------------------------------------
mkdir -p "${OUTPUT_DIR}/trace-output"
mkdir -p "${OUTPUT_DIR}/config"

# ------------------------------------------------------------------
# Create a sample omnistat CSV for testing
# ------------------------------------------------------------------
cat > "${OUTPUT_DIR}/config/sample_omnistat.csv" << 'EOF'
timestamp,hostname,gpu_id,metric_name,metric_value
1718500000000,corona-node-01,0,hip_memory_copy,1024.5
1718500001000,corona-node-01,0,hip_memory_copy,2048.3
1718500002000,corona-node-01,0,hip_memory_copy,512.1
1718500003000,corona-node-01,0,hip_kernel_exec,3.14
1718500004000,corona-node-01,0,hip_kernel_exec,2.71
EOF

echo "=== Build and setup complete ==="
echo "Output directory: ${OUTPUT_DIR}"
echo "Sample config: ${OUTPUT_DIR}/config/sample_omnistat.csv"
echo "Build log: ${BUILD_DIR}/build.log"
