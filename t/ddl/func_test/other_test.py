import pytest
from psycopg2 import sql
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *


def test_ping(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    command = create_resp_req(" ".join(["ping"]))
    answer = create_resp_simple_string("PONG")
    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"

def test_PING(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    command = create_resp_req(" ".join(["PING"]))
    answer = create_resp_simple_string("PONG")
    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"