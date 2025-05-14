
def create_resp_req(request):
    parts = request.split()
    resp_message = f"*{len(parts)}\r\n"
    for part in parts:
        resp_message += f"${len(part)}\r\n{part}\r\n"
    return resp_message.encode('utf-8')


def create_resp_simple_string(request):
    resp_message = f"+{request}\r\n"
    return resp_message.encode('utf-8')

def create_key(table, column, value, separator='.'):
    return f"{table}{separator}{column}{separator}{value}"

def create_value(kv):
    pairs = [f"{key}:{str(value)}" for key, value in kv.items()]
    return ".".join(pairs)

def create_resp_bulk_string(string):
    if string is None:
        return b"$-1\r\n"
    return f"${len(string)}\r\n{string}\r\n".encode('utf-8')

def create_resp_integer(number):
    return f":{number}\r\n".encode('utf-8')

def create_resp_error(message):
    return f"-{message}\r\n".encode('utf-8')

def create_resp_array(items):
    if items is None:
        return b"*-1\r\n"
    resp_message = f"*{len(items)}\r\n"
    for item in items:
        if isinstance(item, str):
            resp_message += create_resp_bulk_string(item).decode('utf-8')
        elif isinstance(item, int):
            resp_message += create_resp_integer(item).decode('utf-8')
        elif isinstance(item, list):
            resp_message += create_resp_array(item).decode('utf-8')
        elif isinstance(item, dict):
            # Для словарей можно использовать вложенные массивы
            resp_message += create_resp_array([f"{k}:{v}" for k, v in item.items()]).decode('utf-8')
        elif item is None:
            resp_message += create_resp_bulk_string(None).decode('utf-8')
        else:
            raise ValueError(f"Unsupported type: {type(item)}")
    
    return resp_message.encode('utf-8')