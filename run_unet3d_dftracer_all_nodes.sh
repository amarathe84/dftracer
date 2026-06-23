#!/bin/bash
# Run dlio_benchmark Unet3D with dftracer + Omnistat for multiple node counts (1, 2, 4).
#
# This script:
#   1. Generates Unet3D training data (one-time setup)
#   2. Runs dlio_benchmark workload=unet3d_dftracer with dftracer tracing for each node count
#   3. Stores trace data in distinct directories under /p/vast1/.../Unet3d/nodes_<N>/
#
# dftracer trace output is in dft.log (Python instrumentation mode).
# The trace captures all I/O, compute, and data loading events from the dlio_benchmark.
#
# Usage:
#   ./run_unet3d_dftracer_all_nodes.sh          # Interactive (run locally on allocated nodes)
#   ./run_unet3d_dftracer_all_nodes.sh --dry-run # Show commands without running
#
# Prerequisites:
#   - For local runs: salloc -n N (N = 1, 2, or 4) before running
#   - dftracer must be installed and available
#   - dlio_benchmark installed (via corona-benchmarks editable install)

set -euo pipefail

WORKSPACE="/usr/WS2/marathe1/dldl/dftracer-omnistat"
DLIO_BENCHMARK_DIR="${WORKSPACE}/dlio_benchmark"
DLIO_BENCHMARK_SRC="${WORKSPACE}/dlio_benchmark/dlio_benchmark"
CORONA_BENCHMARKS="${WORKSPACE}/../corona-benchmarks/dlio_benchmark"

# Output base: traces go here, one subdirectory per node count
OUTPUT_BASE="/p/vast1/marathe1/dldl/dftracer-omnistat/Unet3d"
DATA_DIR="${OUTPUT_BASE}/data/unet3d"
CHECKPOINT_DIR="${OUTPUT_BASE}/checkpoints/unet3d"

# Python executable (must be the system Python 3.13 with ROCm support)
PYTHON="/usr/tce/bin/python3"
# PYTHONPATH must include corona-benchmarks dlio_benchmark for imports
export PYTHONPATH="${CORONA_BENCHMARKS}:${DLIO_BENCHMARK_SRC}:${PYTHONPATH:-}"

# dftracer enable
export DFTRACER_ENABLE=1

# Node counts to run
NODES_LIST=(1 2 4)

# ============================================================
# Step 1: Generate Unet3D training data (one-time setup)
# ============================================================
generate_data() {
    echo "============================================================"
    echo " Step 1: Generating Unet3D training data"
    echo "============================================================"
    
    if [ -d "${DATA_DIR}/train" ] && find "${DATA_DIR}/train" -name "*.npz" 2>/dev/null | head -1 | grep -q .; then
        echo "==> Data already exists at ${DATA_DIR}, skipping generation"
        return 0
    fi

    mkdir -p "${DATA_DIR}/train" "${DATA_DIR}/valid"
    mkdir -p "${CHECKPOINT_DIR}"

    cd "${DLIO_BENCHMARK_DIR}"

    ${PYTHON} /usr/WS2/marathe1/dldl/benchmarks/corona-benchmarks/dlio_benchmark/dlio_benchmark/main.py \
        workload=unet3d_dftracer \
        ++workload.workflow.generate_data=True \
        ++workload.workflow.train=False \
        ++workload.dataset.data_folder="${DATA_DIR}/" \
        ++workload.checkpoint.checkpoint_folder="${CHECKPOINT_DIR}" \
        ++output.folder="${DATA_DIR}" \
        2>&1

    local file_count
    file_count=$(find "${DATA_DIR}/train" -name "*.npz" | wc -l)
    echo "==> Unet3D data generated. Files: ${file_count}"
}

# ============================================================
# Step 2: Run dlio_benchmark for a given node count
# ============================================================
run_benchmark() {
    local nodes=$1
    local run_id
    run_id="$(date +%Y-%m-%d-%H-%M-%S)"

    local run_dir="${OUTPUT_BASE}/nodes_${nodes}/${run_id}"
    local traces_dir="${run_dir}/traces"
    local hydra_dir="${run_dir}/hydra_log"

    mkdir -p "${traces_dir}" "${hydra_dir}"

    echo "============================================================"
    echo " Step 2: Running dlio_benchmark Unet3D (nodes=${nodes})"
    echo "============================================================"
    echo " Run ID:    ${run_id}"
    echo " Output:    ${run_dir}"
    echo " Traces:    ${traces_dir}"
    echo " Nodes:     ${nodes}"
    echo "============================================================"

    # Run dlio_benchmark with dftracer tracing
    # dftracer trace goes to output.folder/trace-{rank}.pfw (Python mode: dft.log)
    # hydra.run.dir controls where hydra stores config/metadata
    export DFTRACER_ENABLE=1
    export DFTRACER_LOG_LEVEL=DEBUG

    ${PYTHON} /usr/WS2/marathe1/dldl/benchmarks/corona-benchmarks/dlio_benchmark/dlio_benchmark/main.py \
        workload=unet3d_dftracer \
        ++workload.workflow.generate_data=False \
        ++workload.workflow.train=True \
        ++workload.workflow.checkpoint=False \
        ++workload.dataset.data_folder="${DATA_DIR}/" \
        ++workload.train.epochs=5 \
        ++workload.checkpoint.checkpoint_folder="${CHECKPOINT_DIR}" \
        ++output.folder="${traces_dir}" \
        ++output.log_file="dlio.log" \
        ++hydra.run.dir="${hydra_dir}" \
        2>&1 | tee "${hydra_dir}/run.log"

    local rc
    rc=${PIPESTATUS:-0}
    echo "==> dlio_benchmark exit code: ${rc} (nodes=${nodes})"

    # Verify trace files exist
    local dft_log
    dft_log="${hydra_dir}/dft.log"
    if [ -f "${dft_log}" ]; then
        echo "==> dft.log (dftracer trace): $(ls -lh "${dft_log}" | awk '{print $5}')"
    else
        echo "==> WARNING: dft.log not found at ${dft_log}"
    fi
    echo "==> Trace directory: ${traces_dir}"
    echo "==> Run completed successfully for nodes=${nodes}"
    echo ""
}

# ============================================================
# Main
# ============================================================
main() {
    local mode="run"
    if [[ "${1:-}" == "--dry-run" ]]; then
        mode="dry-run"
    fi

    # Generate data first
    generate_data

    if [[ "${mode}" == "dry-run" ]]; then
        echo "============================================================"
        echo " DRY RUN: Commands that would be executed:"
        echo "============================================================"
        for nodes in "${NODES_LIST[@]}"; do
            run_id="$(date +%Y-%m-%d-%H-%M-%S)"
            run_dir="${OUTPUT_BASE}/nodes_${nodes}/${run_id}"
            traces_dir="${run_dir}/traces"
            hydra_dir="${run_dir}/hydra_log"
            echo ""
            echo "--- nodes=${nodes} ---"
            echo "  mkdir -p ${traces_dir} ${hydra_dir}"
            echo "  export DFTRACER_ENABLE=1 DFTRACER_LOG_LEVEL=DEBUG"
            echo "  /usr/tce/bin/python3 /usr/WS2/marathe1/dldl/benchmarks/corona-benchmarks/dlio_benchmark/dlio_benchmark/main.py \\"
            echo "    workload=unet3d_dftracer \\"
            echo "    ++workload.workflow.generate_data=False \\"
            echo "    ++workload.workflow.train=True \\"
            echo "    ++workload.workflow.checkpoint=False \\"
            echo "    ++workload.dataset.data_folder='${DATA_DIR}/' \\"
            echo "    ++workload.train.epochs=5 \\"
            echo "    ++workload.checkpoint.checkpoint_folder='${CHECKPOINT_DIR}' \\"
            echo "    ++output.folder='${traces_dir}' \\"
            echo "    ++output.log_file='dlio.log' \\"
            echo "    ++hydra.run.dir='${hydra_dir}'"
        done
        echo ""
        echo "==> Trace data will be stored in:"
        for nodes in "${NODES_LIST[@]}"; do
            echo "  ${OUTPUT_BASE}/nodes_${nodes}/<timestamp>/hydra_log/dft.log"
        done
        return 0
    fi

    # Interactive run: run all node counts
    for nodes in "${NODES_LIST[@]}"; do
        run_benchmark "${nodes}"
    done
}

main "$@"
