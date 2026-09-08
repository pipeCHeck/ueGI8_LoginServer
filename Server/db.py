import os

import pymysql

GAME_SERVER_HEARTBEAT_INTERVAL_SECONDS = 10
GAME_SERVER_TTL_SECONDS = 30
DB_PASSWORD_ENVIRONMENT_VARIABLE = "L20260713_DB_PASSWORD"

DB_CONFIG = dict(
    host="127.0.0.1",
    port=3306,
    user="root",
    db="seul",
    charset="utf8mb4",
)


def get_connection():
    password = os.environ.get(DB_PASSWORD_ENVIRONMENT_VARIABLE)
    if password is None:
        raise RuntimeError(
            f"{DB_PASSWORD_ENVIRONMENT_VARIABLE} environment variable is not set"
        )

    connection_config = {**DB_CONFIG, "password": password}
    return pymysql.connect(
        **connection_config,
        cursorclass=pymysql.cursors.DictCursor,
    )


def initialize_database():
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                """
                CREATE TABLE IF NOT EXISTS game_server (
                    server_id CHAR(36) PRIMARY KEY,
                    host VARCHAR(255) NOT NULL,
                    port SMALLINT UNSIGNED NOT NULL,
                    created_at DATETIME(6) NOT NULL,
                    last_heartbeat DATETIME(6) NOT NULL,
                    INDEX idx_game_server_last_heartbeat (last_heartbeat)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
                """
            )
        conn.commit()
    finally:
        conn.close()
