
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