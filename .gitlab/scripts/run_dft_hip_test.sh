#!/bin/bash

export DFTRACER_ENABLE=1

cd $CUSTOM_CI_BUILDS_DIR

# Clone if it doesn't exist
if [ ! -d "$CUSTOM_CI_BUILDS_DIR/dftracer" ]; then
    git clone $DFTRACER_REPO $CUSTOM_CI_BUILDS_DIR/dftracer
fi

cd dftracer

git checkout $CI_COMMIT_REF_NAME

export QUEUE=pdebug
export WALLTIME=1h

source ${CUSTOM_CI_ENV_DIR}/$ENV_NAME/bin/activate

flux run -N1 -q $QUEUE -t $WALLTIME --exclusive python3 test/py/hip_test.py
