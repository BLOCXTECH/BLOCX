#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-blocxpay/blocxd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/blocxd docker/bin/
cp $BUILD_DIR/src/blocx-cli docker/bin/
cp $BUILD_DIR/src/blocx-tx docker/bin/
strip docker/bin/blocxd
strip docker/bin/blocx-cli
strip docker/bin/blocx-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
