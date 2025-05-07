import subprocess

import pytest


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

def parse_rps_from_output(output):
    """Парсит RPS из вывода redis-benchmark"""
    if not output:
        return 0.0
    for line in output.split('\n'):
        if "throughput summary:" in line:
            # Разбиваем строку по пробелам и берем третий элемент (индекс 2)
            return float(line.split()[2])
    return 0.0

def redis_bench_create_set(pipelines, port, clients, requests, threads, table_name, columns):
    """
    Generates a redis-benchmark command for load testing

    Parameters:
        pipelines (int): number of pipelines (-P)
        port (int): Redis port (-p)
        clients (int): number of clients (-c)
        requests (int): number of requests (-n)
        threads (int): number of threads (--threads)
        table_name (str): database name
        columns (list): list of column names

    Returns:
        str: formatted redis-benchmark command
    """
    # Base command parameters
    command_parts = [
        "redis-benchmark",
        "-P", str(pipelines),
        "-d", "32",
        "-p", str(port),
        "-c", str(clients),
        "-n", str(requests),
        "--threads", str(threads),
        "-r", "10000",
        "set"
    ]

    # Form the key
    key = f"'{table_name}.{columns[0]}.__rand_int__'"
    # Form the value from all columns
    value_parts = [f"{col}:__rand_int__" for col in columns]
    value = "'" + ".".join(value_parts) + "'"

    # Assemble the full command
    command_parts.extend([key, value])

    command = " ".join(command_parts)
    return command

def redis_bench_create_get(pipelines, port, clients, requests, threads, table_name, key_column):
    """
    Generates a redis-benchmark command for load testing

    Parameters:
        pipelines (int): number of pipelines (-P)
        port (int): Redis port (-p)
        clients (int): number of clients (-c)
        requests (int): number of requests (-n)
        threads (int): number of threads (--threads)
        table_name (str): database name
        columns (list): list of column names

    Returns:
        str: formatted redis-benchmark command
    """
    # Base command parameters
    command_parts = [
        "redis-benchmark",
        "-P", str(pipelines),
        "-d", "32",
        "-p", str(port),
        "-c", str(clients),
        "-n", str(requests),
        "--threads", str(threads),
        "-r", "10000",
        "get",
        f"'{table_name}.{key_column}.__rand_int__'"
    ]

    command = " ".join(command_parts)
    return command

def redis_bench_create_del(pipelines, port, clients, requests, threads, table_name, key_column):
    """
    Generates a redis-benchmark command for load testing

    Parameters:
        pipelines (int): number of pipelines (-P)
        port (int): Redis port (-p)
        clients (int): number of clients (-c)
        requests (int): number of requests (-n)
        threads (int): number of threads (--threads)
        table_name (str): database name
        columns (list): list of column names

    Returns:
        str: formatted redis-benchmark command
    """
    # Base command parameters
    command_parts = [
        "redis-benchmark",
        "-P", str(pipelines),
        "-d", "32",
        "-p", str(port),
        "-c", str(clients),
        "-n", str(requests),
        "--threads", str(threads),
        "-r", "10000",
        "del",
        f"'{table_name}.{key_column}.__rand_int__'"
    ]

    command = " ".join(command_parts)
    return command

def prepare_test_data(table_name, benchmark_params):
    """Заполняет базу тестовыми данными перед удалением"""
    print(f"{Colors.OKGREEN}\n=== Подготовка тестовых данных ===")
    print(f"Заполнение таблицы {table_name} тестовыми значениями{Colors.ENDC}")

    
    
    # Генерируем команду для заполнения
    fill_cmd = redis_bench_create_set(**benchmark_params)
    print(f"{Colors.OKBLUE}Команда заполнения: {fill_cmd}{Colors.ENDC}")
    
    try:
        subprocess.run(
            fill_cmd,
            shell=True,
            check=True,
            executable='/bin/bash',
            capture_output=False
        )
    except subprocess.CalledProcessError as e:
        pytest.fail(f"{Colors.FAIL}Ошибка при заполнении тестовых данных: {e.returncode}{Colors.ENDC}")