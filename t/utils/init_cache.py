
from t.utils.db_connect import *
from t.utils.create_resp import *

def init_cache_tt(table_name, columns, count_val):
    sock = create_socket()

    for i in range (0, count_val):
        kv = {
            columns[0]: str(i),
            columns[1]: str(i)
        }
        key = create_key(table_name, columns[0], str(i))
        value = create_value(kv)
        command = create_resp_req(" ".join(["set", key, value]))
        answer = create_resp_simple_string("OK")
        sock.sendall(command)



