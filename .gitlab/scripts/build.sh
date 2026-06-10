#!/bin/bash

LOG_FILE="$PWD/build.log"

# Clear or initialize the log file
: > "$LOG_FILE"

echo "Running build.sh on $(hostname)" | tee -a "$LOG_FILE"

CC=$(which mpicc)
CXX=$(which mpic++)
export CC
export CXX
export MPICC="$CC"

if [ -z "$CC" ] || [ -z "$CXX" ]; then
    echo "Error: Could not resolve mpicc/mpic++ from PATH." | tee -a "$LOG_FILE"
    exit 1
fi

echo "Using CC=$CC" | tee -a "$LOG_FILE"
echo "Using CXX=$CXX" | tee -a "$LOG_FILE"

# Resolve the python site-packages path safely
site=$(ls -d "${CUSTOM_CI_ENV_DIR}/$ENV_NAME"/lib/python*/site-packages/ 2>>"$LOG_FILE")
export site

# Verify that $site was found to prevent dangerous 'rm -rf /*' behavior
if [ -z "$site" ]; then
    echo "Error: Could not determine python site-packages directory." | tee -a "$LOG_FILE"
    exit 1
fi

echo "Remove preinstall version of dlio_benchmark" | tee -a "$LOG_FILE"
echo "Command: pip uninstall dlio_benchmark" | tee -a "$LOG_FILE"
if ! pip uninstall -y dlio_benchmark >>"$LOG_FILE" 2>&1; then
    echo "Failed to uninstall dlio_benchmark. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

if ! rm -rf "$site"/*dlio_benchmark* >>"$LOG_FILE" 2>&1; then
    echo "Failed to remove dlio_benchmark files. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Remove preinstall version of mpi4py" | tee -a "$LOG_FILE"
echo "Command: pip uninstall mpi4py" | tee -a "$LOG_FILE"
if ! pip uninstall -y mpi4py >>"$LOG_FILE" 2>&1; then
    echo "Failed to uninstall mpi4py. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

if ! rm -rf "$site"/mpi4py* >>"$LOG_FILE" 2>&1; then
    echo "Failed to remove mpi4py files. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Remove preinstall version of dftracer" | tee -a "$LOG_FILE"
echo "Command: pip uninstall dftracer" | tee -a "$LOG_FILE"
if ! pip uninstall -y dftracer >>"$LOG_FILE" 2>&1; then
    echo "Failed to uninstall dftracer. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

if ! rm -rf "$site"/*dftracer* >>"$LOG_FILE" 2>&1; then
    echo "Failed to remove dftracer files. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Installing mpi4py, DLIO benchmark, and DFTracer together" | tee -a "$LOG_FILE"
echo "Command: pip install --no-cache-dir --force-reinstall --no-binary=mpi4py mpi4py git+${DLIO_BENCHMARK_REPO}@${DLIO_BENCHMARK_TAG} git+${DFTRACER_REPO}@${CI_COMMIT_REF_NAME}" | tee -a "$LOG_FILE"
if ! pip install --no-cache-dir --force-reinstall --no-binary=mpi4py mpi4py "git+${DLIO_BENCHMARK_REPO}@${DLIO_BENCHMARK_TAG}" "git+${DFTRACER_REPO}@${CI_COMMIT_REF_NAME}" >>"$LOG_FILE" 2>&1; then
    echo "Failed to install mpi4py, DLIO benchmark, and DFTracer. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Install gitlab requirements" | tee -a "$LOG_FILE"
echo "Command: pip install -r .gitlab/scripts/requirements.txt" | tee -a "$LOG_FILE"
if ! pip install -r .gitlab/scripts/requirements.txt >>"$LOG_FILE" 2>&1; then
    echo "Failed to install gitlab requirements. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

python -c "import dftracer; import dftracer.python; print(dftracer.__version__);"
export PATH="$site/dftracer/bin:$PATH"