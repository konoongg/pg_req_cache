# fixtures/table_fixtures.py
import pytest
import psycopg2


@pytest.fixture(scope="function")
def create_table_text_text(create_and_drop_db):
    db_name = create_and_drop_db

    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS test_table (
            column1 TEXT UNIQUE,
            column2 TEXT
        );
    """)
    conn.commit()

    yield  cursor

    cursor.close()
    conn.close()

@pytest.fixture(scope="function")
def open_table(create_and_drop_db):
    db_name = create_and_drop_db

    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()

    yield  cursor

    cursor.execute("DROP TABLE IF EXISTS test_table;")
    conn.commit()

    cursor.close()
    conn.close()


@pytest.fixture(scope="function")
def create_table_text_int(create_and_drop_db):
    db_name = create_and_drop_db

    conn = psycopg2.connect(
        dbname=db_name, host="localhost"
    )
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS test_table (
            column1 TEXT UNIQUE,
            column2 INT
        );
    """)
    conn.commit()

    yield conn, cursor

    cursor.execute("DROP TABLE IF EXISTS test_table;")
    conn.commit()

    cursor.close()
    conn.close()
