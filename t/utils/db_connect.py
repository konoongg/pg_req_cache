from psycopg2 import sql

import psycopg2
import pytest
import socket
import subprocess

POSTGRESQL_PATH = "/home/konoongg/home/postgres"
DB_PATH = "/home/konoongg/home/work/db"

def create_table_text_text(db_name):
    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()

    table_name = "test_table"

    query = sql.SQL("""
    CREATE TABLE IF NOT EXISTS {} (
        column1 TEXT UNIQUE,
        column2 TEXT
    );
    """).format(sql.Identifier(table_name))

    cursor.execute(query)
    conn.commit()

    cursor.close()
    conn.close()

    return table_name


def create_table_text_int(db_name):
    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()

    table_name = "test_table"

    query = sql.SQL("""
    CREATE TABLE IF NOT EXISTS {} (
        column1 TEXT UNIQUE,
        column2 INT
    );
    """).format(sql.Identifier(table_name))

    cursor.execute(query)
    conn.commit()

    cursor.close()
    conn.close()

    return table_name




def restart_postgres():
    try:
        subprocess.run(["make", "clean", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"], check=True)
        subprocess.run(["make", "install", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"], check=True)
        subprocess.run(["pg_ctl", "-D", "redis_proxy", "stop", "-m", "immediate"], cwd=DB_PATH, check=True)
        subprocess.run(["pg_ctl", "-D", "redis_proxy", "-l", "logfile", "start"], cwd=DB_PATH, check=True)
    except subprocess.CalledProcessError as e:
        pytest.fail(f"Ошибка при выполнении команд: {e}")


def open_table(db_name):
    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()
    return cursor

def create_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", 6379))
    return sock
