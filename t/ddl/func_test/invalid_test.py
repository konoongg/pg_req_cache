import traceback
import pytest
from psycopg2 import sql
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *


def test_flag_invalid_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_bd(db_name)
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
    cursor = open_bd(db_name)
    sock = create_socket()

    try:
        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }

        ttl = 5  # 5 секунд

        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        command = create_resp_req(" ".join(["set", key, value, "EX", str(ttl)]))
        answer = create_resp_simple_string("OK")

        sock.sendall(command)
        response = sock.recv(1024)
        assert response == answer, (
            f"Ожидался ответ '{answer.decode('utf-8')}', "
            f"но получен: '{response.decode('utf-8')}'"
        )

        # Проверяем, что данные сохранились
        command = create_resp_req(" ".join(["get", key]))
        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_response = create_resp_array([expected_data])

        sock.sendall(command)
        get_response = sock.recv(1024)
        assert get_response == expected_response, (
            f"Ожидался ответ '{expected_response.decode('utf-8')}', "
            f"но получен: '{get_response.decode('utf-8')}'"
        )

        # Обновляем данные в PostgreSQL вручную
        updated_values = {
            columns[0]: kv[columns[0]],
            columns[1]: "test4"  # Новое значение
        }

        update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[1]),
            sql.Identifier(columns[0])
        )
        cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
        cursor.connection.commit()

        # Проверяем, что данные обновились в PostgreSQL
        select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0]),
            sql.Identifier(columns[1])
        )
        cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
        updated_result = cursor.fetchone()
        assert updated_result is not None, (
            f"Данные не найдены в PostgreSQL после обновления. "
            f"Искали: {updated_values}"
        )

        # Ждём истечения TTL
        time.sleep(ttl + 1)  # +1 секунда для надёжности

        # Проверяем, что данные всё ещё доступны (хотя TTL истёк)
        updated_inner_array = [updated_values[columns[0]], updated_values[columns[1]]]
        updated_outer_array = [updated_inner_array]
        updated_expected_response = create_resp_array(updated_outer_array)

        sock.sendall(command)
        updated_get_response = sock.recv(1024)

        assert updated_get_response == updated_expected_response, (
            f"Ожидался ответ '{updated_expected_response.decode('utf-8')}', "
            f"но получен: '{updated_get_response.decode('utf-8')}'. "
            f"TTL истёк, но данные всё ещё доступны."
        )

    except Exception as e:
        pytest.fail(f"Тест упал с ошибкой: {str(e)}")
    finally:
        # Cleanup resources
        if sock:
            sock.close()
        if cursor:
            if cursor.connection:
                cursor.connection.close()
            cursor.close()


def test_time_ms_invalid_table_tt(create_and_drop_db, cleanup_schema):
    # Initialize resources
    sock = None
    cursor = None
    try:
        # Setup test environment
        db_name = create_and_drop_db
        table_name, columns = create_table_text_text(db_name)
        restart_postgres()
        cursor = open_bd(db_name)
        sock = create_socket()

        # Test data
        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }
        ttl = 5000  # 5 seconds in milliseconds

        # SET command
        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        set_command = create_resp_req(" ".join(["set", key, value, "PX", str(ttl)]))
        expected_set_response = create_resp_simple_string("OK")

        sock.sendall(set_command)
        set_response = sock.recv(1024)
        assert set_response == expected_set_response, (
            f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
            f"got: {set_response.decode('utf-8') if set_response else 'None'}"
        )

        # GET command to verify initial data
        get_command = create_resp_req(" ".join(["get", key]))
        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_get_response = create_resp_array([expected_data])

        sock.sendall(get_command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

        # Manually update data in PostgreSQL
        updated_values = {
            columns[0]: kv[columns[0]],
            columns[1]: "test4"  # New value
        }

        update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[1]),
            sql.Identifier(columns[0])
        )
        cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
        cursor.connection.commit()

        # Verify update in PostgreSQL
        select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0]),
            sql.Identifier(columns[1])
        )
        cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
        updated_result = cursor.fetchone()
        assert updated_result is not None, (
            f"Data not updated in PostgreSQL. Expected: {updated_values}, "
            f"found: None"
        )

        # Wait for TTL to expire (adding small buffer)
        time.sleep((ttl / 1000) + 0.5)

        # Verify data is still accessible after TTL expiration
        updated_expected_data = [updated_values[columns[0]], updated_values[columns[1]]]
        updated_expected_response = create_resp_array([updated_expected_data])

        sock.sendall(get_command)
        updated_get_response = sock.recv(1024)

        assert updated_get_response == updated_expected_response, (
            f"Data should be accessible after TTL. "
            f"Expected: {updated_expected_response.decode('utf-8')}, "
            f"got: {updated_get_response.decode('utf-8') if updated_get_response else 'None'}"
        )

    except Exception as e:
        # Include stack trace in failure message
        pytest.fail(f"Test failed with exception: {str(e)}\n{traceback.format_exc()}")
    finally:
        # Cleanup resources
        if sock:
            sock.close()
        if cursor:
            if cursor.connection:
                cursor.connection.close()
            cursor.close()

def test_event_single_update_invalid_table_tt(create_and_drop_db, cleanup_schema):
    sock = None
    cursor = None
    try:
        db_name = create_and_drop_db
        table_name, columns = create_table_text_text(db_name)
        restart_postgres()
        cursor = open_bd(db_name)
        sock = create_socket()

        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }
        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        set_command = create_resp_req(" ".join(["set", key, value]))
        expected_set_response = create_resp_simple_string("OK")

        sock.sendall(set_command)
        set_response = sock.recv(1024)
        assert set_response == expected_set_response, (
            f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
            f"got: {set_response.decode('utf-8') if set_response else 'None'}"
        )


        get_command = create_resp_req(" ".join(["get", key]))
        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_get_response = create_resp_array([expected_data])

        sock.sendall(get_command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

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

        # Verify update in PostgreSQL
        select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0]),
            sql.Identifier(columns[1])
        )
        cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
        updated_result = cursor.fetchone()
        assert updated_result is not None, (
            f"Data not updated in PostgreSQL. Expected: {updated_values}, "
            f"found: None"
        )

        time.sleep(1)

        # Verify data is still accessible after TTL expiration
        updated_expected_data = [updated_values[columns[0]], updated_values[columns[1]]]
        updated_expected_response = create_resp_array([updated_expected_data])

        sock.sendall(get_command)
        updated_get_response = sock.recv(1024)

        assert updated_get_response == updated_expected_response, (
            f"Data should be accessible after TTL. "
            f"Expected: {updated_expected_response.decode('utf-8')}, "
            f"got: {updated_get_response.decode('utf-8') if updated_get_response else 'None'}"
        )

    except Exception as e:
        # Include stack trace in failure message
        pytest.fail(f"Test failed with exception: {str(e)}\n{traceback.format_exc()}")
    finally:
        # Cleanup resources
        if sock:
            sock.close()
        if cursor:
            if cursor.connection:
                cursor.connection.close()
            cursor.close()


def test_event_transaction_update_invalid_table_tt(create_and_drop_db, cleanup_schema):
    sock = None
    cursor = None
    try:
        db_name = create_and_drop_db
        table_name, columns = create_table_text_text(db_name)
        restart_postgres()
        cursor = open_bd(db_name)
        sock = create_socket()

        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }
        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        set_command = create_resp_req(" ".join(["set", key, value]))
        expected_set_response = create_resp_simple_string("OK")

        sock.sendall(set_command)
        set_response = sock.recv(1024)
        assert set_response == expected_set_response, (
            f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
            f"got: {set_response.decode('utf-8') if set_response else 'None'}"
        )


        get_command = create_resp_req(" ".join(["get", key]))
        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_get_response = create_resp_array([expected_data])

        sock.sendall(get_command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

        updated_values = {
            columns[0]: kv[columns[0]],
            columns[1]: "test4"
        }

        update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[1]),
            sql.Identifier(columns[0])
        )
        cursor.execute("BEGIN")
        cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
        cursor.execute("COMMIT")

        # Verify update in PostgreSQL
        select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0]),
            sql.Identifier(columns[1])
        )
        cursor.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
        updated_result = cursor.fetchone()
        assert updated_result is not None, (
            f"Data not updated in PostgreSQL. Expected: {updated_values}, "
            f"found: None"
        )

        time.sleep(1)

        # Verify data is still accessible after TTL expiration
        updated_expected_data = [updated_values[columns[0]], updated_values[columns[1]]]
        updated_expected_response = create_resp_array([updated_expected_data])

        sock.sendall(get_command)
        updated_get_response = sock.recv(1024)

        assert updated_get_response == updated_expected_response, (
            f"Data should be accessible after TTL. "
            f"Expected: {updated_expected_response.decode('utf-8')}, "
            f"got: {updated_get_response.decode('utf-8') if updated_get_response else 'None'}"
        )

    except Exception as e:
        # Include stack trace in failure message
        pytest.fail(f"Test failed with exception: {str(e)}\n{traceback.format_exc()}")
    finally:
        # Cleanup resources
        if sock:
            sock.close()
        if cursor:
            if cursor.connection:
                cursor.connection.close()
            cursor.close()


def test_event_transaction_rollback_invalid_table_tt(create_and_drop_db, cleanup_schema):
    sock = None
    cursor = None
    try:
        db_name = create_and_drop_db
        table_name, columns = create_table_text_text(db_name)
        restart_postgres()
        cursor = open_bd(db_name)
        sock = create_socket()

        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }
        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        set_command = create_resp_req(" ".join(["set", key, value]))
        expected_set_response = create_resp_simple_string("OK")

        sock.sendall(set_command)
        set_response = sock.recv(1024)
        assert set_response == expected_set_response, (
            f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
            f"got: {set_response.decode('utf-8') if set_response else 'None'}"
        )


        get_command = create_resp_req(" ".join(["get", key]))


        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_get_response = create_resp_array([expected_data])

        sock.sendall(get_command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

        updated_values = {
            columns[0]: kv[columns[0]],
            columns[1]: "test4"
        }

        update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[1]),
            sql.Identifier(columns[0])
        )
        cursor.execute("BEGIN")
        cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
        cursor.execute("ROLLBACK")

        # Verify update in PostgreSQL
        select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0]),
            sql.Identifier(columns[1])
        )

        cursor.execute(select_query, (kv[columns[0]], kv[columns[1]]))
        updated_result = cursor.fetchone()
        assert updated_result is not None, (
            f"Data not updated in PostgreSQL. Expected: {kv}, "
            f"found: None"
        )

        time.sleep(1)

        sock.sendall(get_command)
        get_response = sock.recv(1024)

        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

    except Exception as e:
        # Include stack trace in failure message
        pytest.fail(f"Test failed with exception: {str(e)}\n{traceback.format_exc()}")
    finally:
        # Cleanup resources
        if sock:
            sock.close()
        if cursor:
            cursor.close()
            if cursor.connection:
                cursor.connection.close()


def test_event_commit_update_invalid_table_tt_no_wait(create_and_drop_db, cleanup_schema):
    sock = None
    cursor = None
    try:
        db_name = create_and_drop_db
        table_name, columns = create_table_text_text(db_name)
        restart_postgres()
        cursor = open_bd(db_name)
        sock = create_socket()

        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }
        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        set_command = create_resp_req(" ".join(["set", key, value]))
        expected_set_response = create_resp_simple_string("OK")

        sock.sendall(set_command)
        set_response = sock.recv(1024)
        assert set_response == expected_set_response, (
            f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
            f"got: {set_response.decode('utf-8') if set_response else 'None'}"
        )


        get_command = create_resp_req(" ".join(["get", key]))
        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_get_response = create_resp_array([expected_data])

        sock.sendall(get_command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

        updated_values = {
            columns[0]: kv[columns[0]],
            columns[1]: "test4"
        }

        update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[1]),
            sql.Identifier(columns[0])
        )
        cursor.execute("BEGIN")
        cursor.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))
        cursor.execute("COMMIT")

        # Verify data is still accessible after TTL expiration
        updated_expected_data = [updated_values[columns[0]], updated_values[columns[1]]]
        updated_expected_response = create_resp_array([updated_expected_data])

        sock.sendall(get_command)
        updated_get_response = sock.recv(1024)

        assert updated_get_response == updated_expected_response, (
            f"Data should be accessible after TTL. "
            f"Expected: {updated_expected_response.decode('utf-8')}, "
            f"got: {updated_get_response.decode('utf-8') if updated_get_response else 'None'}"
        )

    except Exception as e:
        pytest.fail(f"Test failed with exception: {str(e)}\n{traceback.format_exc()}")
    finally:
        # Cleanup resources
        if sock:
            sock.close()
        if cursor:
            cursor.close()
            if cursor.connection:
                cursor.connection.close()


def test_event_single_delete_invalid_table_tt(create_and_drop_db, cleanup_schema):
    sock = None
    cursor = None
    try:
        db_name = create_and_drop_db
        table_name, columns = create_table_text_text(db_name)
        restart_postgres()
        cursor = open_bd(db_name)
        sock = create_socket()

        kv = {
            columns[0]: "test1",
            columns[1]: "test2"
        }
        key = create_key(table_name, columns[0], "test1")
        value = create_value(kv)
        set_command = create_resp_req(" ".join(["set", key, value]))
        expected_set_response = create_resp_simple_string("OK")

        sock.sendall(set_command)
        set_response = sock.recv(1024)
        assert set_response == expected_set_response, (
            f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
            f"got: {set_response.decode('utf-8') if set_response else 'None'}"
        )

        get_command = create_resp_req(" ".join(["get", key]))
        expected_data = [kv[columns[0]], kv[columns[1]]]
        expected_get_response = create_resp_array([expected_data])

        sock.sendall(get_command)
        get_response = sock.recv(1024)
        assert get_response == expected_get_response, (
            f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
            f"got: {get_response.decode('utf-8') if get_response else 'None'}"
        )

        # DELETE с правильным форматированием
        delete_query = sql.SQL("DELETE FROM {} WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0])
        )
        cursor.execute(delete_query, (kv[columns[0]],))

        # SELECT с правильным форматированием
        select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s").format(
            sql.Identifier(table_name),
            sql.Identifier(columns[0])
        )
        cursor.execute(select_query, (kv[columns[0]],))

        deleted_result = cursor.fetchone()
        assert deleted_result is None, (
            f"Data still exists in PostgreSQL after DELETE. Expected: None, "
            f"got: {deleted_result}"
        )

        time.sleep(1)

        delete_expected_response = create_resp_array(None)
        sock.sendall(get_command)
        delete_get_response = sock.recv(1024)

        assert delete_get_response == delete_expected_response, (
            f"Data should be inaccessible after DELETE. "
            f"Expected: {delete_expected_response.decode('utf-8')}, "
            f"got: {delete_get_response.decode('utf-8') if delete_get_response else 'None'}"
        )

    except Exception as e:
        pytest.fail(f"Test failed with exception: {str(e)}\n{traceback.format_exc()}")
    finally:
        if sock:
            sock.close()
        if cursor:
            if cursor.connection:
                cursor.connection.close()
            cursor.close()


def test_replica_invalid(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_cluster()
    cursor_master = open_bd(db_name, 5432)
    cursor_replica = open_bd(db_name, 5433)

    sock_master = create_socket(6379)
    sock_replica = create_socket(6380)

    kv = {
        columns[0]: "test1",
        columns[1]: "test2"
    }
    key = create_key(table_name, columns[0], "test1")
    value = create_value(kv)
    set_command = create_resp_req(" ".join(["set", key, value]))
    get_command = create_resp_req(" ".join(["get", key]))

    expected_set_response = create_resp_simple_string("OK")
    sock_master.sendall(set_command)
    set_response = sock_master.recv(1024)
    assert set_response == expected_set_response, (
        f"SET failed. Expected: {expected_set_response.decode('utf-8')}, "
        f"got: {set_response.decode('utf-8') if set_response else 'None'}"
    )

    time.sleep(10)

    expected_data = [kv[columns[0]], kv[columns[1]]]
    expected_get_response = create_resp_array([expected_data])

    sock_master.sendall(get_command)
    get_response = sock_master.recv(1024)
    assert get_response == expected_get_response, (
        f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
        f"got: {get_response.decode('utf-8') if get_response else 'None'}"
    )

    sock_replica.sendall(get_command)
    get_response = sock_replica.recv(1024)
    assert get_response == expected_get_response, (
        f"Initial GET failed. Expected: {expected_get_response.decode('utf-8')}, "
        f"got: {get_response.decode('utf-8') if get_response else 'None'}"
    )


    updated_values = {
        columns[0]: kv[columns[0]],
        columns[1]: "test4"
    }

    update_query = sql.SQL("UPDATE {} SET {} = %s WHERE {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[1]),
        sql.Identifier(columns[0])
    )
    cursor_master.execute(update_query, (updated_values[columns[1]], updated_values[columns[0]]))

    # Verify update in PostgreSQL
    select_query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[0]),
        sql.Identifier(columns[1])
    )
    cursor_master.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
    updated_result = cursor_master.fetchone()
    assert updated_result is not None, (
        f"Data not updated in PostgreSQL. Expected: {updated_values}, "
        f"found: None"
    )

    cursor_replica.execute(select_query, (updated_values[columns[0]], updated_values[columns[1]]))
    updated_result = cursor_replica.fetchone()
    assert updated_result is not None, (
        f"Data not updated in PostgreSQL. Expected: {updated_values}, "
        f"found: None"
    )

    time.sleep(1)

    # Verify data is still accessible after TTL expiration
    updated_expected_data = [updated_values[columns[0]], updated_values[columns[1]]]
    updated_expected_response = create_resp_array([updated_expected_data])

    sock_replica.sendall(get_command)
    updated_get_response = sock_replica.recv(1024)

    assert updated_get_response == updated_expected_response, (
        f"Data should be accessible after TTL. "
        f"Expected: {updated_expected_response.decode('utf-8')}, "
        f"got: {updated_get_response.decode('utf-8') if updated_get_response else 'None'}"
    )
