import pytest
from psycopg2 import sql
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *


def test_flag_invalid_table_tt(create_and_drop_db, cleanup_schema):
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

    updated_values = {
        columns[0]: kv[columns[0]],
        columns[1]: "test4"
    }

    update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[1]),
        sql.Identifier(columns[0])
    )

    cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
    cursor.connection.commit()


    select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[0]),
        sql.Identifier(columns[1])
    )
    cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
    updated_result = cursor.fetchone()
    assert updated_result is not None, "Updated data not found in PostgreSQL"

    d_key = "[D]" + key
    command = create_resp_req(" ".join(["get", d_key]))
    updated_inner_array = [updated_values[columns[0]], updated_values[columns[1]]]
    updated_outer_array = [updated_inner_array]
    updated_expected_response = create_resp_array(updated_outer_array)
    sock.sendall(command)
    updated_get_response = sock.recv(1024)
    assert updated_get_response == updated_expected_response, \
        f"Expected {updated_expected_response}, got: {updated_get_response}"


def test_time_s_invalid_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    kv = {
        columns[0]: "test1",
        columns[1]: "test2"
    }

    ttl = 5

    key = create_key(table_name, columns[0], "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value, "EX", str(ttl)]))
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

    updated_values = {
        columns[0]: kv[columns[0]],
        columns[1]: "test4"
    }

    update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[1]),
        sql.Identifier(columns[0])
    )

    cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
    cursor.connection.commit()


    select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[0]),
        sql.Identifier(columns[1])
    )
    cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
    updated_result = cursor.fetchone()
    assert updated_result is not None, "Updated data not found in PostgreSQL"

    time.sleep(ttl)

    updated_inner_array = [updated_values[columns[0]], updated_values[columns[1]]]
    updated_outer_array = [updated_inner_array]
    updated_expected_response = create_resp_array(updated_outer_array)
    sock.sendall(command)
    updated_get_response = sock.recv(1024)
    assert updated_get_response == updated_expected_response, \
        f"Expected {updated_expected_response}, got: {updated_get_response}"


def test_time_ms_invalid_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_table(db_name)
    sock = create_socket()

    kv = {
        columns[0]: "test1",
        columns[1]: "test2"
    }

    ttl = 5000

    key = create_key(table_name, columns[0], "test1")
    value = create_value(kv)
    command = create_resp_req(" ".join(["set", key, value, "PX", str(ttl)]))
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

    updated_values = {
        columns[0]: kv[columns[0]],
        columns[1]: "test4"
    }

    update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[1]),
        sql.Identifier(columns[0])
    )

    cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
    cursor.connection.commit()


    select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[0]),
        sql.Identifier(columns[1])
    )
    cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
    updated_result = cursor.fetchone()
    assert updated_result is not None, "Updated data not found in PostgreSQL"

    time.sleep(ttl / 1000)

    updated_inner_array = [updated_values[columns[0]], updated_values[columns[1]]]
    updated_outer_array = [updated_inner_array]
    updated_expected_response = create_resp_array(updated_outer_array)
    sock.sendall(command)
    updated_get_response = sock.recv(1024)
    assert updated_get_response == updated_expected_response, \
        f"Expected {updated_expected_response}, got: {updated_get_response}"