import pytest
from t.fixtures.table_fixtures import *
from t.fixtures.db_fixtures import *
from t.fixtures.pg_req_fixtures import *
from t.fixtures.restart_fixures import *


def test_simple_set_table_tt(create_table_text_text, restart_postgresql, open_table, req_socket):
    cursor = open_table
    socket = req_socket

    command = b"*3\r\n$3\r\nset\r\n$24\r\ntest_table.column1.test1\r\n$27\r\ncolumn1:test1.column2:test2\r\n"
    socket.sendall(command)

    response = socket.recv(1024)
    assert response == b"+OK\r\n", f"Ожидался ответ '+OK\r\n', но получен: {response}"

    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test1", "test2"))
    result = cursor.fetchone()

    assert result is not None, "Строка с column1=test1 и column2=test2 не найдена в таблице"

def test_simple_double_set_table_tt(create_table_text_text, restart_postgresql, open_table, req_socket):
    cursor = open_table
    socket = req_socket

    command = b"*3\r\n$3\r\nset\r\n$24\r\ntest_table.column1.test1\r\n$27\r\ncolumn1:test1.column2:test2\r\n"
    socket.sendall(command)
    response = socket.recv(1024)
    assert response == b"+OK\r\n", f"Ожидался ответ '+OK\r\n', но получен: {response}"
    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test1", "test2"))
    result = cursor.fetchone()
    assert result is not None, "Строка с column1=test1 и column2=test2 не найдена в таблице"

    command = b"*3\r\n$3\r\nset\r\n$24\r\ntest_table.column1.test1\r\n$27\r\ncolumn1:test2.column2:test3\r\n"
    socket.sendall(command)
    response = socket.recv(1024)
    assert response == b"+OK\r\n", f"Ожидался ответ '+OK\r\n', но получен: {response}"
    cursor.execute("SELECT * FROM test_table WHERE column1 = %s AND column2 = %s", ("test1", "test2"))
    result = cursor.fetchone()
    assert result is not None, "Строка с column1=test2 и column2=test3 не найдена в таблице"

# def test_simple_set_table_ti(open_table_text_int, req_socket):
    