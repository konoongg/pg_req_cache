import pytest
import psycopg2
from psycopg2 import sql

Host = "localhost"
DBname = "postgres"
PORT = 5432
USER = "postgres"

import time
import psycopg2
import pytest

@pytest.fixture(scope="function")
def create_and_drop_db():
    max_retries = 10
    retry_delay = 1
    conn = None
    cursor = None
    db_name = "postgres"

    for attempt in range(1, max_retries + 1):
        try:
            conn = psycopg2.connect(
                dbname=DBname,
                host=Host,
                user = USER,
                port = PORT,
                connect_timeout=2
            )
            conn.autocommit = True
            cursor = conn.cursor()

            cursor.execute("SELECT 1 FROM pg_database WHERE datname = %s", (db_name,))
            exists = cursor.fetchone()

            if not exists:
                cursor.execute(f"CREATE DATABASE {db_name}")
            yield db_name
            if not exists:
                try:
                    cursor.execute(f"DROP DATABASE {db_name}")
                except Exception as drop_error:
                    print(f"Ошибка при удалении базы данных: {drop_error}")
            break
        except Exception as e:
            print(f"Ошибка при попытке #{attempt}: {str(e)}")
            if attempt < max_retries:
                time.sleep(retry_delay)
                continue
            pytest.fail(f"Не удалось выполнить create_and_drop_db после {max_retries} попыток")
        finally:
            if cursor:
                cursor.close()
            if conn:
                conn.close()


@pytest.fixture(scope="function")
def cleanup_schema():
    yield

    max_retries = 3
    retry_delay = 1

    for attempt in range(1, max_retries + 1):
        try:
            conn = psycopg2.connect(
                dbname=DBname,
                host=Host,
                user=USER,
                connect_timeout=2
            )
            conn.autocommit = True
            cursor = conn.cursor()

            cursor.execute("""
                DO $$ DECLARE
                    r RECORD;
                BEGIN
                    FOR r IN (SELECT tablename FROM pg_tables WHERE schemaname = 'public') LOOP
                        EXECUTE 'DROP TABLE IF EXISTS ' || quote_ident(r.tablename) || ' CASCADE';
                    END LOOP;
                END $$;
            """)

            cursor.close()
            conn.close()
            return

        except Exception as e:
            print(f" \n Ошибка при попытке #{attempt}: {str(e)}")
            if attempt < max_retries:
                time.sleep(retry_delay)
                continue
            pytest.fail(f"Не удалось подключиться после {max_retries} попыток")
