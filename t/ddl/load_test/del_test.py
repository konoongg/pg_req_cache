import sys
import pytest
from t.utils.redis_bench_creater import *
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *

class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


def test_t1_p1_n100000_del_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres(100)

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 1,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "key_column": columns[0],
    }

    prepare_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "columns": columns,
    }

    prepare_test_data(table_name, prepare_params)
    # Generate benchmark command
    benchmark_cmd = redis_bench_create_del(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"{Colors.OKBLUE} \nExecuting Redis benchmark {i}: {benchmark_cmd} {Colors.ENDC}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")

def test_t4_p1_n100000_del_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres(100)

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 1,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 4,
        "table_name": table_name,
        "key_column": columns[0],
    }

    prepare_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "columns": columns,
    }

    prepare_test_data(table_name, prepare_params)
    # Generate benchmark command
    benchmark_cmd = redis_bench_create_del(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"{Colors.OKBLUE} \nExecuting Redis benchmark {i}: {benchmark_cmd} {Colors.ENDC}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")

def test_t4_p4_n100000_del_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres(100)

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 4,
        "table_name": table_name,
        "key_column": columns[0],
    }
    prepare_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "columns": columns,
    }

    prepare_test_data(table_name, prepare_params)
    # Generate benchmark command
    benchmark_cmd = redis_bench_create_del(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"{Colors.OKBLUE} \nExecuting Redis benchmark {i}: {benchmark_cmd} {Colors.ENDC}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")


def test_t8_p8_n100000_del_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres(100)

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 8,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 8,
        "table_name": table_name,
        "key_column": columns[0],
    }
    prepare_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "columns": columns,
    }

    prepare_test_data(table_name, prepare_params)
    # Generate benchmark command
    benchmark_cmd = redis_bench_create_del(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"{Colors.OKBLUE} \nExecuting Redis benchmark {i}: {benchmark_cmd} {Colors.ENDC}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")



def test_t1_p4_n100000_del_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres(100)

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "key_column": columns[0],
    }
    prepare_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 100000,
        "threads": 1,
        "table_name": table_name,
        "columns": columns,
    }

    prepare_test_data(table_name, prepare_params)
    # Generate benchmark command
    benchmark_cmd = redis_bench_create_del(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"{Colors.OKBLUE} \nExecuting Redis benchmark {i}: {benchmark_cmd} {Colors.ENDC}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")