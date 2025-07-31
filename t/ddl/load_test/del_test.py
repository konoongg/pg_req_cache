# import csv
# import sys
# import pytest
# from t.utils.init_cache import init_cache_tt
# from t.utils.redis_bench_creater import *
# from t.fixtures.db_fixtures import *
# from t.utils.create_resp import *
# from t.utils.db_connect import *

# SYSTEM_NAME = "pgcache"  # Название системы (настраивается в скрипте)
# PROGRAM_VERSION = "0.6"       # Версия программы
# DB_STATE = "10000 записей, два текстового столбца"  # Состояние БД
# THREADS_IN_SYSTEM = 4

# WORKERS = [1, 4, 8, 16]
# PIPELINE_COUNT = [1, 4, 8, 16]
# REQUEST_COUNT = [100000]
# CLIENT_COUNT = [100, 1000, 5000, 10000]
# PORT = [6379]

# RESULTS_FILE = "benchmark_results.csv"

# try:
#     with open(RESULTS_FILE, 'x', newline='') as f:
#         writer = csv.writer(f)
#         writer.writerow([
#             "Операция",
#             "Потоки в системе",
#             "Система",
#             "Workers",
#             "Pipeline",
#             "Request_count",
#             "Client_count",
#             "Описание",
#             "RPS",
#             "Версия программы",
#             "Комментарий",
#             "Состояние БД",
#         ])
# except FileExistsError:
#     pass



# @pytest.mark.parametrize("workers", WORKERS, ids=lambda x: f"workers={x}")
# @pytest.mark.parametrize("pipeline", PIPELINE_COUNT, ids=lambda x: f"pipeline={x}")
# @pytest.mark.parametrize("request_count", REQUEST_COUNT, ids=lambda x: f"request_count={x}")
# @pytest.mark.parametrize("client_count", CLIENT_COUNT, ids=lambda x: f"client_count={x}")
# @pytest.mark.parametrize("port_num", PORT, ids=lambda x: f"port_num={x}")
# def test_del(create_and_drop_db, workers, pipeline, request_count, client_count, port_num, cleanup_schema):
#     db_name = create_and_drop_db
#     table_name, columns = create_table_text_text(db_name)
#     restart_postgres()

#     init_cache_tt(table_name, columns, 10000)

#     # Redis benchmark setup
#     benchmark_params = {
#         "pipelines": pipeline,
#         "port": port_num,
#         "clients": client_count,
#         "requests": request_count,
#         "threads": workers,
#         "table_name": table_name,
#         "key_column": columns[0],
#     }

#     # Generate benchmark command
#     benchmark_cmd = redis_bench_create_del(**benchmark_params)
#     print(f"\nExecuting Redis benchmark: {benchmark_cmd}")
#     try:
#         result = subprocess.run(
#             benchmark_cmd,
#             shell=True,
#             check=True,
#             executable='/bin/bash',
#             capture_output=True,
#             text = True
#         )
#         rps = parse_rps_from_output(result.stdout)

#         with open(RESULTS_FILE, 'a', newline='') as f:
#                 writer = csv.writer(f)
#                 writer.writerow([
#                     "DEL",
#                     THREADS_IN_SYSTEM,
#                     SYSTEM_NAME,
#                     workers,
#                     pipeline,
#                     request_count,
#                     client_count,
#                     "",
#                     rps,
#                     PROGRAM_VERSION,
#                     "",
#                     DB_STATE,
#                 ])

#     except subprocess.CalledProcessError as e:
#         pytest.fail(f"Benchmark failed with code {e.returncode}")