import pymysql

GAME_SERVER_HEARTBEAT_INTERVAL_SECONDS = 10
GAME_SERVER_TTL_SECONDS = 30

DB_CONFIG = dict(
    host="127.0.0.1",
    port=3306,
    user="root",
    password="qweasd123",
    db="seul",
    charset="utf8mb4",
)


def get_connection():
    return pymysql.connect(**DB_CONFIG, cursorclass=pymysql.cursors.DictCursor)


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
