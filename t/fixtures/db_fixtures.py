import pytest
import psycopg2
from psycopg2 import sql

Host = "localhost"
DBname = "postgres"

@pytest.fixture(scope="function")
def create_and_drop_db():
    conn = psycopg2.connect(
        dbname=DBname, host=Host
    )
    conn.autocommit = True
    cursor = conn.cursor()

    db_name = "postgres"

    cursor.execute("SELECT 1 FROM pg_database WHERE datname = %s", (db_name,))
    exists = cursor.fetchone()

    if not exists:
        cursor.execute(f"CREATE DATABASE {db_name}")

    yield db_name

    if not exists:
        cursor.execute(f"DROP DATABASE {db_name}")

    cursor.close()
    conn.close()

@pytest.fixture(scope="function")
def cleanup_schema():
    yield

    conn = psycopg2.connect(
        dbname=DBname, host=Host
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