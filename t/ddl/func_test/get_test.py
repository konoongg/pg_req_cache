import pytest
from psycopg2 import sql
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *

COUNT_MANY_REQ = 100

def test_simple_get_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    kv = {
        columns[0]: "test1",
        columns[1]: "test2"
    }

    key = create_key(table_name, columns[0], "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value]))
    answer = create_resp_simple_string("OK")

    sock.sendall(command)

    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"
    answer = create_resp_simple_string("OK")

    command = create_resp_req(" ".join(["get", key]))

    inner_array = ["test1", "test2"]
    outer_array = [inner_array]
    expected_get_response = create_resp_array(outer_array)
    sock.sendall(command)
    get_response = sock.recv(1024)
    assert get_response == expected_get_response, \
        f"Ожидался ответ {expected_get_response}, но получен: {get_response}"

def test_many_get_table_from_db_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    for i in range (0,COUNT_MANY_REQ):
        test_values = {
            columns[0]: "test" + str(i),
            columns[1]: "test" + str(i)
        }

        insert_query = sql.SQL("INSERT INTO {} ({}, {}) VALUES (%s, %s)").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0]),
            sql.Identifier(columns[1])
        )
        cursor.execute(insert_query, (test_values[columns[0]], test_values[columns[1]]))
        cursor.connection.commit()

        key = create_key(table_name, columns[0], test_values[columns[0]])

        command = create_resp_req(" ".join(["get", key]))

        inner_array = [test_values[columns[0]], test_values[columns[1]]]
        outer_array = [inner_array]
        expected_get_response = create_resp_array(outer_array)
        sock.sendall(command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, \
            f"Ожидался ответ {expected_get_response}, но получен: {get_response}"
