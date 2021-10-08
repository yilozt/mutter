#!/bin/bash

work_dir=$(cd $(dirname $0); cd ../; pwd)
cd $work_dir

mkdir -p build compile

cd build
if [ -z "$(ls -A .)" ]; then
    meson ../
fi

ninja && meson install --destdir=${work_dir}/compile