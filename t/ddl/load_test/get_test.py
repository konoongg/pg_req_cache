import sys
import pytest
from t.utils.redis_bench_creater import *
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *

def test_t1_p1_n1000000_get_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres(100)
    cursor = open_table(db_name)
    sock = create_socket()

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 1,
        "port": 6379,
        "clients": 1000,
        "requests": 1000000,
        "threads": 1,
        "db_name": table_name,
        "columns": ["column1", "column2"],
        "command": "get"
    }

    # Generate benchmark command
    benchmark_cmd = redis_bench_create(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")

def test_t4_p1_n1000009_get_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres(100)
    cursor = open_table(db_name)
    sock = create_socket()

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 1,
        "port": 6379,
        "clients": 1000,
        "requests": 1000000,
        "threads": 4,
        "db_name": table_name,
        "columns": ["column1", "column2"],
        "command": "get"
    }

    # Generate benchmark command
    benchmark_cmd = redis_bench_create(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")

def test_t4_p4_n1000000_get_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres(100)
    cursor = open_table(db_name)
    sock = create_socket()

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 1000000,
        "threads": 4,
        "db_name": table_name,
        "columns": ["column1", "column2"],
        "command": "get"
    }

    # Generate benchmark command
    benchmark_cmd = redis_bench_create(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")


def test_t8_p8_n1000000_get_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres(100)
    cursor = open_table(db_name)
    sock = create_socket()

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 8,
        "port": 6379,
        "clients": 1000,
        "requests": 1000000,
        "threads": 8,
        "db_name": table_name,
        "columns": ["column1", "column2"],
        "command": "get"
    }

    # Generate benchmark command
    benchmark_cmd = redis_bench_create(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")



def test_t1_p4_n1000000_get_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres(100)
    cursor = open_table(db_name)
    sock = create_socket()

    # Redis benchmark setup
    benchmark_params = {
        "pipelines": 4,
        "port": 6379,
        "clients": 1000,
        "requests": 1000000,
        "threads": 1,
        "db_name": table_name,
        "columns": ["column1", "column2"],
        "command": "get"
    }

    # Generate benchmark command
    benchmark_cmd = redis_bench_create(**benchmark_params)
    print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
    for i in range (5):
        try:
            print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
            subprocess.run(
                benchmark_cmd,
                shell=True,
                check=True,
                executable='/bin/bash',
                capture_output=False
            )
        except subprocess.CalledProcessError as e:
            pytest.fail(f"Benchmark failed with code {e.returncode}")