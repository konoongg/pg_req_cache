#!/bin/bash
ulimit -c unlimited 
sudo sysctl  -p
clear
export PYTHONDONTWRITEBYTECODE=1

case "$1" in
    func)
        pytest ${PYTEST_OPTIONS} -s -v ddl/func_test/
        ;;
    load)
        pytest ${PYTEST_OPTIONS} -s -v ddl/load_test/
        ;;
    all)
        pytest ${PYTEST_OPTIONS} -s -v ddl/func_test/ ddl/load_test/
        ;;
    *)
        echo "Usage: $0 {func|load|all}"
        exit 1
        ;;
esac
