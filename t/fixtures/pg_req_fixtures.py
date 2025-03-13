import pytest
import socket

@pytest.fixture(scope="function")
def req_socket(open_table):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    sock.connect(("localhost", 6379))
    yield sock
    sock.close()