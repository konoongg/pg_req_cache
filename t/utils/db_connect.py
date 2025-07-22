import time
from typing import List
from psycopg2 import sql

import psycopg2
import pytest
import socket
import subprocess

POSTGRESQL_PATH = "/home/konoongg/home/postgres"
DB_PATH = "/home/konoongg/home/work/db"

def create_table_text_text(db_name):
    conn = psycopg2.connect(
        dbname=db_name, host="localhost", user="postgres"
    )
    cursor = conn.cursor()

    table_name = "test"
    columns = ["column1", "column2"]
    query = sql.SQL("""
    CREATE TABLE IF NOT EXISTS {} (
        column1 TEXT PRIMARY KEY,
        column2 TEXT
    );
    """).format(sql.Identifier(table_name))

    cursor.execute(query)
    conn.commit()

    cursor.close()
    conn.close()

    return table_name, columns


def create_table_text_int(db_name):
    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()

    table_name = "test_table"
    columns = ["column1", "column2"]
    query = sql.SQL("""
    CREATE TABLE IF NOT EXISTS {} (
        column1 TEXT PRIMARY KEY,
        column2 INT
    );
    """).format(sql.Identifier(table_name))

    cursor.execute(query)
    conn.commit()

    cursor.close()
    conn.close()

    return table_name, columns


def restart_cluster(timeout: int = 100):
    """Перезапускает PostgreSQL без вывода команд в консоль"""
    commands: List[List[str]] = [
        ["make", "clean", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"],
        ["make", "install", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"],
        ["pg_ctl", "-D", "redis_proxy", "stop", "-m", "immediate"],
        ["pg_ctl", "-D", "master/data", "stop", "-m", "immediate"],
        ["pg_ctl", "-D", "replica/data", "stop", "-m", "immediate"],

        ["pg_ctl", "-D", "master/data", "-l", "master/logfile", "start"],
        ["pg_ctl", "-D", "replica/data", "-l", "replica/logfile", "start"],
    ]

    for cmd in commands:
        try:
            subprocess.run(
                cmd,
                cwd=DB_PATH if "pg_ctl" in cmd[0] else None,
                check=True,
                timeout=timeout,
                stdout=subprocess.DEVNULL,  # Подавляем stdout
                stderr=subprocess.DEVNULL   # Подавляем stderr
            )
        except subprocess.TimeoutExpired:
            pytest.fail(f"Команда {cmd} не завершилась за {timeout} секунд")
        except subprocess.CalledProcessError as e:
            pass
    time.sleep(2)


def restart_postgres():
    """Перезапускает PostgreSQL без вывода команд в консоль"""
    commands: List[List[str]] = [
        ["make", "clean", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"],
        ["make", "install", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"],
        ["pg_ctl", "-D", "master/data", "stop", "-m", "immediate"],
        ["pg_ctl", "-D", "replica/data", "stop", "-m", "immediate"],
        ["pg_ctl", "-D", "redis_proxy", "stop", "-m", "immediate"],
        ["pg_ctl", "-D", "redis_proxy", "-l", "logfile", "start"],
    ]

    for cmd in commands:
        try:
            subprocess.run(
                cmd,
                cwd=DB_PATH if "pg_ctl" in cmd[0] else None,
                check=True,
                stdout=subprocess.DEVNULL,  # Подавляем stdout
                stderr=subprocess.DEVNULL   # Подавляем stderr
            )
        except subprocess.CalledProcessError as e:
            pass
    time.sleep(1)


def open_bd(db_name, port = 5432):
    conn = psycopg2.connect(
        dbname=db_name, host="localhost", port = port, user="postgres"
    )
    cursor = conn.cursor()
    cursor.connection.autocommit = True
    return cursor

def create_socket(port = 6379):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.connect(("localhost", port))
    sock.settimeout(1.0)
    return sock
