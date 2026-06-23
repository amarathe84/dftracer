#!/bin/bash
#
# Test DFTracer HIP tracing on AMD MI300A
# Submits a flux job that runs a simple HIP application with DFTracer
#
# Usage: sbatch test_hip_trace.sh
#
#SBATCH --job-name=dftracer-hip-test
#SBATCH --partition=pdebug
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --time=01:00:00
#SBATCH --output=/p/vast1/marathe1/dldl/dftrcer-omnistat/test-%j.out
#SBATCH --error=/p/vast1/marathe1/dldl/dftrcer-omnistat/test-%j.err

set -euo pipefail

# ------------------------------------------------------------------
# Paths
# ------------------------------------------------------------------
export WORKSPACE="/usr/WS2/marathe1/dldl/dftracer-omnistat"
export BUILD_DIR="${WORKSPACE}/build-omnistat-hip"
export INSTALL_PREFIX="${WORKSPACE}/.local-hip"
export OUTPUT_DIR="/p/vast1/marathe1/dldl/dftrcer-omnistat"
export TRACE_DIR="${OUTPUT_DIR}/trace-output"

echo "=== DFTracer HIP Trace Test ==="
echo "BUILD_DIR: ${BUILD_DIR}"
echo "TRACE_DIR: ${TRACE_DIR}"

# ------------------------------------------------------------------
# Load modules
# ------------------------------------------------------------------
module purge
module load StdEnv
module load amd/6.4.3
module load cray-python/3.12.12

# ------------------------------------------------------------------
# Set up library paths for the built DFTracer
# ------------------------------------------------------------------
export LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib64:${INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="${BUILD_DIR}/lib64:${LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="${INSTALL_PREFIX}/libexec/rocprofiler-sdk:${LD_LIBRARY_PATH}"

# Find rocprofiler SDK libs
for rocm_dir in /opt/rocm-6.4.3 /opt/rocm-6.4.2 /opt/rocm-6.4.1 /opt/rocm-6.4.0 /opt/rocm-7.0.2 /opt/rocm-7.0.1 /opt/rocm-7.0.0 /opt/rocm-6.2.4 /opt/rocm-6.2.1 /opt/rocm-6.2.0; do
    if [ -d "${rocm_dir}/libexec/rocprofiler-sdk" ]; then
        export LD_LIBRARY_PATH="${rocm_dir}/libexec/rocprofiler-sdk:${LD_LIBRARY_PATH}"
    fi
    if [ -d "${rocm_dir}/lib" ]; then
        export LD_LIBRARY_PATH="${rocm_dir}/lib:${LD_LIBRARY_PATH}"
    fi
done

export LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH}"

# ------------------------------------------------------------------
# Set DFTracer environment
# ------------------------------------------------------------------
export DFTRACER_ENABLE=1
export DFTRACER_LOG_FILE="${TRACE_DIR}/test_trace"
export DFTRACER_DATA_DIR="/dev/shm"
export DFTRACER_LOG_LEVEL=INFO

# ------------------------------------------------------------------
# Create omnistat config
# ------------------------------------------------------------------
cat > "${TRACE_DIR}/omnistat_config.yaml" << EOF
enable: true
tracer:
  log_file: ${TRACE_DIR}/test_trace
  data_dirs: /dev/shm
features:
  omnistat:
    enable: true
    input: ${OUTPUT_DIR}/config/sample_omnistat.csv
    format: csv
    timestamp_column: timestamp
    include_all_counters: true
    attach_to_trace: true
    export_raw: false
    time_sync:
      mode: absolute
EOF

export DFTRACER_CONFIGURATION="${TRACE_DIR}/omnistat_config.yaml"

echo "=== DFTracer Configuration ==="
echo "DFTRACER_ENABLE: ${DFTRACER_ENABLE}"
echo "DFTRACER_LOG_FILE: ${DFTRACER_LOG_FILE}"
echo "DFTRACER_DATA_DIR: ${DFTRACER_DATA_DIR}"
echo "DFTRACER_CONFIGURATION: ${DFTRACER_CONFIGURATION}"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"

# ------------------------------------------------------------------
# Run a simple test: start and stop the DFTracer service
# ------------------------------------------------------------------
echo "=== Running DFTracer service test ==="

# Check if the service binary exists
SERVICE_BIN="${BUILD_DIR}/dftracer_service"
if [ ! -x "${SERVICE_BIN}" ]; then
    echo "ERROR: DFTracer service binary not found at ${SERVICE_BIN}"
    echo "Build may have failed. Check ${BUILD_DIR}/build.log"
    exit 1
fi

echo "Starting DFTracer service..."
# Start the service in background
"${SERVICE_BIN}" start "${TRACE_DIR}" &
SERVICE_PID=$!
echo "Service PID: ${SERVICE_PID}"

# Wait for service to initialize
sleep 3

# Stop the service
"${SERVICE_BIN}" stop "${TRACE_DIR}"
wait "${SERVICE_PID}" 2>/dev/null || true

echo "=== Checking output ==="
echo "Trace files:"
ls -la "${TRACE_DIR}"/*.pfw* 2>/dev/null || echo "No .pfw files found"
ls -la "${TRACE_DIR}"/*.pfw.gz 2>/dev/null || echo "No .pfw.gz files found"

# ------------------------------------------------------------------
# Validate trace files
# ------------------------------------------------------------------
echo "=== Validating traces ==="
VALIDATE_BIN="${BUILD_DIR}/dftracer_validate"
if [ -x "${VALIDATE_BIN}" ]; then
    "${VALIDATE_BIN}" -d "${TRACE_DIR}"
else
    echo "dftracer_validate not found, checking manually..."
    for f in "${TRACE_DIR}"/*.pfw*; do
        if [ -f "$f" ]; then
            echo "File: $f"
            if [[ "$f" == *.gz ]]; then
                gunzip -c "$f" | head -5
            else
                head -5 "$f"
            fi
        fi
    done
fi

echo "=== Test complete ==="
