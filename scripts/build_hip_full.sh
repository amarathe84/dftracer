#!/bin/bash
#
# Full build script for DFTracer with HIP tracing enabled
# Designed for flux-managed cluster with AMD MI300A GPUs
#
# Usage:
#   # Interactive build:
#   bash scripts/build_hip_full.sh
#
#   # Or submit as a batch job:
#   sbatch scripts/build_hip_full.sh
#

set -euo pipefail

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------
export WORKSPACE="/usr/WS2/marathe1/dldl/dftracer-omnistat"
export BUILD_DIR="${WORKSPACE}/build-omnistat-hip"
export INSTALL_PREFIX="${WORKSPACE}/.local-hip"
export OUTPUT_DIR="/p/vast1/marathe1/dldl/dftrcer-omnistat"

# ------------------------------------------------------------------
# Load modules for HIP/ROCm build
# ------------------------------------------------------------------
module purge
module load StdEnv
module load amd/6.4.3
module load cmake/3.24.2
module load cray-python/3.12.12

echo "=== Loaded modules ==="
module list

# ------------------------------------------------------------------
# Create directories
# ------------------------------------------------------------------
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}/trace-output"
mkdir -p "${OUTPUT_DIR}/config"

# ------------------------------------------------------------------
# Create sample omnistat CSV for testing
# ------------------------------------------------------------------
cat > "${OUTPUT_DIR}/config/sample_omnistat.csv" << 'EOF'
timestamp,hostname,gpu_id,metric_name,metric_value
1718500000000,corona-node-01,0,hip_memory_copy,1024.5
1718500001000,corona-node-01,0,hip_memory_copy,2048.3
1718500002000,corona-node-01,0,hip_memory_copy,512.1
1718500003000,corona-node-01,0,hip_kernel_exec,3.14
1718500004000,corona-node-01,0,hip_kernel_exec,2.71
EOF

# ------------------------------------------------------------------
# Configure CMake
# ------------------------------------------------------------------
echo "=== Configuring CMake ==="
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
    -DDFTRACER_INSTALL_DEPENDENCIES=ON \
    -DDFTRACER_DISABLE_HWLOC=ON \
    -DPYTHON_EXECUTABLE=$(which python3) \
    -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}"

# ------------------------------------------------------------------
# Build dependencies and DFTracer
# ------------------------------------------------------------------
echo "=== Building DFTracer (dependencies + main) ==="
make -C "${BUILD_DIR}" -j$(nproc) 2>&1 | tee "${BUILD_DIR}/build.log"

# ------------------------------------------------------------------
# Install
# ------------------------------------------------------------------
echo "=== Installing DFTracer ==="
make -C "${BUILD_DIR}" install 2>&1 | tee "${BUILD_DIR}/install.log"

# ------------------------------------------------------------------
# Verify build
# ------------------------------------------------------------------
echo "=== Verifying build ==="
echo "Built libraries:"
find "${BUILD_DIR}" -name "*.so" -o -name "*.a" 2>/dev/null | head -20
echo ""
echo "Installed libraries:"
find "${INSTALL_PREFIX}" -name "*.so" -o -name "*.a" 2>/dev/null | head -20

# ------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------
echo ""
echo "============================================"
echo "Build Summary"
echo "============================================"
echo "Build directory:  ${BUILD_DIR}"
echo "Install prefix:   ${INSTALL_PREFIX}"
echo "Output directory: ${OUTPUT_DIR}"
echo "Build log:        ${BUILD_DIR}/build.log"
echo "Install log:      ${BUILD_DIR}/install.log"
echo "Sample config:    ${OUTPUT_DIR}/config/sample_omnistat.csv"
echo "============================================"
