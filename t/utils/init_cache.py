
from t.utils.db_connect import *
from t.utils.create_resp import *

def init_cache_tt(table_name, columns, count_val):
    sock = create_socket()

    for i in range (0, count_val):
        kv = {
            columns[0]: f"{i:012d}",
            columns[1]: f"{i:012d}"
        }
        key = create_key(table_name, columns[0], kv[columns[0]])
        value = create_value(kv)
        command = create_resp_req(" ".join(["set", key, value]))
        sock.sendall(command)
        answer = create_resp_simple_string("OK")
        response = sock.recv(1024)
        assert response == answer, f"Ожидался ответ {answer}, но получен: {response}"



