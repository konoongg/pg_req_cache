import pytest
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *


def test_simple_set_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    kv = {
        "column1": "test1",
        "column2": "test2"
    }

    key = create_key(table_name, "column1", "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value]))
    answer = create_resp_simple_string("OK")
    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"

    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test1", "test2"))
    result = cursor.fetchone()

    assert result is not None, "Строка с column1=test1 и column2=test2 не найдена в таблице"

def test_simple_double_set_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    kv = {
        "column1": "test1",
        "column2": "test2"
    }

    key = create_key(table_name, "column1", "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value]))
    answer = create_resp_simple_string("OK")
    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"

    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test1", "test2"))
    result = cursor.fetchone()
    assert result is not None, "Строка с column1=test1 и column2=test2 не найдена в таблице"

    kv = {
        "column1": "test11",
        "column2": "test22"
    }
    key = create_key(table_name, "column1", "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value]))
    answer = create_resp_simple_string("OK")
    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"
    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test11", "test22"))
    result = cursor.fetchone()
    assert result is not None, "Строка с column1=test11 и column2=test22 не найдена в таблице"

def test_simple_set_table_ti(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name = create_table_text_int(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    kv = {
        "column1": "test1",
        "column2": 1
    }

    key = create_key(table_name, "column1", "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value]))
    answer = create_resp_simple_string("OK")
    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"

    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test1", 1))
    result = cursor.fetchone()
