def redis_bench_create(pipelines, port, clients, requests, threads, db_name, columns, command):
    """
    Generates a redis-benchmark command for load testing

    Parameters:
        pipelines (int): number of pipelines (-P)
        port (int): Redis port (-p)
        clients (int): number of clients (-c)
        requests (int): number of requests (-n)
        threads (int): number of threads (--threads)
        db_name (str): database name
        columns (list): list of column names

    Returns:
        str: formatted redis-benchmark command
    """
    # Base command parameters
    command_parts = [
        "redis-benchmark",
        "-P", str(pipelines),
        "-d", "32",
        "-p", str(port),
        "-c", str(clients),
        "-n", str(requests),
        "--threads", str(threads),
        "-r", "10000",
        command
    ]

    # Form the key
    key = f"'{db_name}.{columns[0]}.__rand_int__'"
    # Form the value from all columns
    value_parts = [f"{col}:__rand_int__" for col in columns]
    value = "'" + ".".join(value_parts) + "'"

    # Assemble the full command
    command_parts.extend([key, value])

    command = " ".join(command_parts)
    return command