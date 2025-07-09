import pytest
from psycopg2 import sql
from t.fixtures.db_fixtures import *
from t.utils.create_resp import *
from t.utils.db_connect import *
#хэлперы для операция и дял бд pydantic
def test_simple_del_table_tt(create_and_drop_db, cleanup_schema):
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

    query = sql.SQL("SELECT * FROM {} WHERE {} = %s AND {} = %s").format(
        sql.Identifier(table_name),
        sql.Identifier(columns[0]),
        sql.Identifier(columns[1])
    )
    cursor.execute(query, ("test1", "test2"))
    result = cursor.fetchone()

    assert result is not None, "Строка с column1=test1 и column2=test2 не найдена в таблице"


    command = create_resp_req(" ".join(["del", key]))
    answer = create_resp_integer(1)
    sock.sendall(command)
    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"

    cursor.execute(query, ("test1", "test2"))
    result = cursor.fetchone()
    assert result is None, "Данные не были удалены из БД"


def test_no_exist_del_table_tt(create_and_drop_db, cleanup_schema):
    db_name = create_and_drop_db
    table_name, columns = create_table_text_text(db_name)
    restart_postgres()
    cursor = open_bd(db_name)
    sock = create_socket()

    key = create_key(table_name, columns[0], "test1")

    command = create_resp_req(" ".join(["del", key]))
    answer = create_resp_integer(0)
    sock.sendall(command)
    response = sock.recv(1024)
    assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"