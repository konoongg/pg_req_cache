#!/bin/bash

clear
export PYTHONDONTWRITEBYTECODE=1

case "$1" in
    functional)
        pytest -s ddl/func_test/
        ;;
    load)
        pytest -s ddl/load_test/
        ;;
    all)
        pytest -s ddl/func_test/ ddl/load_test/
        ;;
    *)
        echo "Usage: $0 {functional|load|all}"
        exit 1
        ;;
esac