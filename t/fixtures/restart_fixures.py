import pytest
import subprocess
import time

POSTGRESQL_PATH = "/home/konoongg/home/postgres"
DB_PATH = "/home/konoongg/home/work/db"

@pytest.fixture(scope="function")
def restart_postgresql():
    try:
        subprocess.run(["make", "clean", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"], check=True)
        subprocess.run(["make", "install", "-C", f"{POSTGRESQL_PATH}/contrib/pg_redis_proxy"], check=True)
        subprocess.run(["pg_ctl", "-D", "redis_proxy", "stop", "-m", "immediate"], cwd=DB_PATH, check=True)
        subprocess.run(["pg_ctl", "-D", "redis_proxy", "-l", "logfile", "start"], cwd=DB_PATH, check=True)
    except subprocess.CalledProcessError as e:
        pytest.fail(f"Ошибка при выполнении команд: {e}")