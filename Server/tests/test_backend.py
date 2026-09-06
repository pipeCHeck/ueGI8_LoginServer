from datetime import datetime, timedelta
from pathlib import Path
import re
import sys
import warnings

import pymysql
import pytest

warnings.filterwarnings(
    "ignore",
    message="The anyio.abc.BlockingPortal alias is deprecated.*",
    category=DeprecationWarning,
    module="starlette.testclient",
)

from fastapi.testclient import TestClient


SERVER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SERVER_DIR))

import db as db_module
import main as main_module


class ClientAddressApp:
    def __init__(self, app, host):
        self.app = app
        self.host = host

    async def __call__(self, scope, receive, send):
        if scope["type"] in {"http", "websocket"}:
            scope = dict(scope)
            scope["client"] = (self.host, 50000)
        await self.app(scope, receive, send)


class FakeDatabase:
    def __init__(self):
        self.now = datetime(2026, 9, 7, 12, 0, 0)
        self.members = [
            {
                "idx": 10,
                "user_id": "tester",
                "passwd": "1234",
                "nickname": "tester",
                "level": 1,
            }
        ]
        self.game_servers = {}
        self.connections = []

    def connect(self):
        connection = FakeConnection(self)
        self.connections.append(connection)
        return connection


class FakeConnection:
    def __init__(self, database):
        self.database = database
        self.closed = False
        self.commit_count = 0

    def cursor(self):
        return FakeCursor(self.database)

    def commit(self):
        self.commit_count += 1

    def close(self):
        self.closed = True


class FakeCursor:
    def __init__(self, database):
        self.database = database
        self.lastrowid = 0
        self.rowcount = 0
        self.result = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def execute(self, query, params=None):
        sql = " ".join(query.split())
        params = params or ()
        self.result = None
        self.rowcount = 0

        if sql.startswith("INSERT INTO member"):
            user_id, passwd, nickname = params
            if any(row["user_id"] == user_id for row in self.database.members):
                raise pymysql.err.IntegrityError(1062, "duplicate user")
            self.lastrowid = max((row["idx"] for row in self.database.members), default=0) + 1
            self.database.members.append(
                {
                    "idx": self.lastrowid,
                    "user_id": user_id,
                    "passwd": passwd,
                    "nickname": nickname,
                    "level": 1,
                }
            )
            self.rowcount = 1
            return self.rowcount

        if sql.startswith("SELECT idx, nickname, level FROM member"):
            user_id, passwd = params
            member = next(
                (
                    row
                    for row in self.database.members
                    if row["user_id"] == user_id and row["passwd"] == passwd
                ),
                None,
            )
            if member is not None:
                self.result = {
                    "idx": member["idx"],
                    "nickname": member["nickname"],
                    "level": member["level"],
                }
                self.rowcount = 1
            return self.rowcount

        if sql.startswith("DELETE FROM game_server WHERE last_heartbeat"):
            ttl_seconds = _interval_seconds(sql)
            cutoff = self.database.now - timedelta(seconds=ttl_seconds)
            expired_ids = [
                server_id
                for server_id, row in self.database.game_servers.items()
                if row["last_heartbeat"] < cutoff
            ]
            for server_id in expired_ids:
                del self.database.game_servers[server_id]
            self.rowcount = len(expired_ids)
            return self.rowcount

        if sql.startswith("INSERT INTO game_server"):
            server_id, host, port = params
            existing = self.database.game_servers.get(server_id)
            created_at = existing["created_at"] if existing else self.database.now
            self.database.game_servers[server_id] = {
                "server_id": server_id,
                "host": host,
                "port": port,
                "created_at": created_at,
                "last_heartbeat": self.database.now,
            }
            self.rowcount = 1 if existing is None else 2
            return self.rowcount

        if sql.startswith("UPDATE game_server SET last_heartbeat"):
            (server_id,) = params
            row = self.database.game_servers.get(server_id)
            if row is not None:
                changed = row["last_heartbeat"] != self.database.now
                row["last_heartbeat"] = self.database.now
                self.rowcount = int(changed)
            return self.rowcount

        if sql.startswith("SELECT 1 FROM game_server WHERE server_id"):
            (server_id,) = params
            if server_id in self.database.game_servers:
                self.result = {"1": 1}
                self.rowcount = 1
            return self.rowcount

        if sql.startswith("DELETE FROM game_server WHERE server_id"):
            (server_id,) = params
            self.rowcount = int(server_id in self.database.game_servers)
            self.database.game_servers.pop(server_id, None)
            return self.rowcount

        if sql.startswith("SELECT server_id, host, port FROM game_server"):
            ttl_seconds = _interval_seconds(sql)
            cutoff = self.database.now - timedelta(seconds=ttl_seconds)
            valid_rows = [
                row
                for row in self.database.game_servers.values()
                if row["last_heartbeat"] >= cutoff
            ]
            valid_rows.sort(key=lambda row: row["server_id"])
            valid_rows.sort(key=lambda row: row["last_heartbeat"], reverse=True)
            if valid_rows:
                row = valid_rows[0]
                self.result = {
                    "server_id": row["server_id"],
                    "host": row["host"],
                    "port": row["port"],
                }
                self.rowcount = 1
            return self.rowcount

        raise AssertionError(f"Unexpected SQL: {sql}")

    def fetchone(self):
        return self.result


def _interval_seconds(sql):
    match = re.search(r"INTERVAL\s+(\d+)\s+SECOND", sql)
    assert match is not None, f"TTL interval missing from SQL: {sql}"
    return int(match.group(1))


@pytest.fixture
def fake_database(monkeypatch):
    database = FakeDatabase()
    monkeypatch.setattr(main_module, "get_connection", database.connect)
    monkeypatch.setattr(main_module, "initialize_database", lambda: None, raising=False)
    return database


@pytest.fixture
def client(fake_database):
    with TestClient(ClientAddressApp(main_module.app, "192.168.0.25")) as test_client:
        yield test_client


def test_signup_success_and_duplicate_failure_preserve_existing_contract(client):
    first = client.post("/signup", json={"user_id": "new-user", "passwd": "pw"})
    duplicate = client.post("/signup", json={"user_id": "new-user", "passwd": "pw"})

    assert first.status_code == 200
    assert first.json() == {
        "result": True,
        "message": "",
        "idx": 11,
        "nickname": "new-user",
        "level": 1,
    }
    assert duplicate.status_code == 200
    assert duplicate.json() == {
        "result": False,
        "message": "이미 존재하는 아이디입니다",
        "idx": 0,
        "nickname": "",
        "level": 0,
    }


def test_login_success_without_server_and_invalid_login_include_null_server(client):
    success = client.post("/login", json={"user_id": "tester", "passwd": "1234"})
    failure = client.post("/login", json={"user_id": "tester", "passwd": "wrong"})

    assert success.status_code == 200
    assert success.json() == {
        "result": True,
        "message": "",
        "idx": 10,
        "nickname": "tester",
        "level": 1,
        "game_server": None,
    }
    assert failure.status_code == 200
    assert failure.json() == {
        "result": False,
        "message": "아이디 또는 비밀번호가 올바르지 않습니다",
        "idx": 0,
        "nickname": "",
        "level": 0,
        "game_server": None,
    }


def test_register_uses_client_host_and_returns_registry_settings(client, fake_database):
    server_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"

    response = client.post(
        "/game-servers/register",
        json={"server_id": server_id, "port": 7777},
    )

    assert response.status_code == 200
    assert response.json() == {
        "result": True,
        "message": "",
        "server_id": server_id,
        "host": "192.168.0.25",
        "port": 7777,
        "heartbeat_interval_seconds": 10,
        "ttl_seconds": 30,
    }
    assert fake_database.game_servers[server_id]["host"] == "192.168.0.25"
    assert all(connection.closed for connection in fake_database.connections)


def test_register_same_server_id_updates_one_row(client, fake_database):
    server_id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
    client.post("/game-servers/register", json={"server_id": server_id, "port": 7777})

    with TestClient(
        ClientAddressApp(main_module.app, "192.168.0.99")
    ) as second_client:
        response = second_client.post(
            "/game-servers/register",
            json={"server_id": server_id, "port": 8888},
        )

    assert response.status_code == 200
    assert len(fake_database.game_servers) == 1
    assert fake_database.game_servers[server_id]["host"] == "192.168.0.99"
    assert fake_database.game_servers[server_id]["port"] == 8888


@pytest.mark.parametrize("port", [0, -1, 65536])
def test_register_rejects_invalid_port(client, port):
    response = client.post(
        "/game-servers/register",
        json={
            "server_id": "cccccccc-cccc-4ccc-8ccc-cccccccccccc",
            "port": port,
        },
    )

    assert response.status_code == 422


def test_registry_endpoints_reject_invalid_uuid(client):
    register = client.post(
        "/game-servers/register",
        json={"server_id": "not-a-uuid", "port": 7777},
    )
    heartbeat = client.post(
        "/game-servers/heartbeat",
        json={"server_id": "not-a-uuid"},
    )
    unregister = client.post(
        "/game-servers/unregister",
        json={"server_id": "not-a-uuid"},
    )

    assert register.status_code == 422
    assert heartbeat.status_code == 422
    assert unregister.status_code == 422


def test_heartbeat_updates_database_time(client, fake_database):
    server_id = "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
    client.post("/game-servers/register", json={"server_id": server_id, "port": 7777})
    registered_at = fake_database.game_servers[server_id]["last_heartbeat"]
    fake_database.now += timedelta(seconds=5)

    response = client.post(
        "/game-servers/heartbeat",
        json={"server_id": server_id},
    )

    assert response.status_code == 200
    assert response.json() == {"result": True, "message": ""}
    assert fake_database.game_servers[server_id]["last_heartbeat"] > registered_at
    assert fake_database.connections[-1].commit_count == 1
    assert fake_database.connections[-1].closed


def test_heartbeat_succeeds_when_database_time_has_not_advanced(client):
    server_id = "44444444-4444-4444-8444-444444444444"
    client.post("/game-servers/register", json={"server_id": server_id, "port": 7777})

    response = client.post(
        "/game-servers/heartbeat",
        json={"server_id": server_id},
    )

    assert response.status_code == 200
    assert response.json() == {"result": True, "message": ""}


def test_heartbeat_returns_404_for_unregistered_server(client):
    response = client.post(
        "/game-servers/heartbeat",
        json={
            "server_id": "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
        },
    )

    assert response.status_code == 404


def test_unregister_is_idempotent(client, fake_database):
    server_id = "ffffffff-ffff-4fff-8fff-ffffffffffff"
    client.post("/game-servers/register", json={"server_id": server_id, "port": 7777})

    first = client.post(
        "/game-servers/unregister",
        json={"server_id": server_id},
    )
    second = client.post(
        "/game-servers/unregister",
        json={"server_id": server_id},
    )

    assert first.status_code == 200
    assert first.json() == {"result": True, "message": ""}
    assert second.status_code == 200
    assert second.json() == {"result": True, "message": ""}
    assert server_id not in fake_database.game_servers
    assert fake_database.connections[-1].commit_count == 1
    assert fake_database.connections[-1].closed


def test_login_returns_newest_valid_server_and_excludes_expired(client, fake_database):
    expired_id = "10000000-0000-4000-8000-000000000000"
    older_valid_id = "20000000-0000-4000-8000-000000000000"
    newest_valid_id = "30000000-0000-4000-8000-000000000000"

    for server_id, port in (
        (expired_id, 7000),
        (older_valid_id, 7001),
        (newest_valid_id, 7002),
    ):
        client.post(
            "/game-servers/register",
            json={"server_id": server_id, "port": port},
        )

    fake_database.game_servers[expired_id]["last_heartbeat"] = (
        fake_database.now - timedelta(seconds=31)
    )
    fake_database.game_servers[older_valid_id]["last_heartbeat"] = (
        fake_database.now - timedelta(seconds=10)
    )
    fake_database.game_servers[newest_valid_id]["last_heartbeat"] = (
        fake_database.now - timedelta(seconds=1)
    )

    response = client.post("/login", json={"user_id": "tester", "passwd": "1234"})

    assert response.status_code == 200
    assert response.json()["game_server"] == {
        "server_id": newest_valid_id,
        "host": "192.168.0.25",
        "port": 7002,
    }


def test_login_breaks_equal_heartbeat_tie_by_server_id(client, fake_database):
    larger_id = "90000000-0000-4000-8000-000000000000"
    smaller_id = "80000000-0000-4000-8000-000000000000"

    client.post("/game-servers/register", json={"server_id": larger_id, "port": 9000})
    client.post("/game-servers/register", json={"server_id": smaller_id, "port": 8000})

    response = client.post("/login", json={"user_id": "tester", "passwd": "1234"})

    assert response.status_code == 200
    assert response.json()["game_server"]["server_id"] == smaller_id


class RecordingSchemaConnection:
    def __init__(self, error=None):
        self.error = error
        self.statements = []
        self.commit_count = 0
        self.closed = False

    def cursor(self):
        return RecordingSchemaCursor(self)

    def commit(self):
        self.commit_count += 1

    def close(self):
        self.closed = True


class RecordingSchemaCursor:
    def __init__(self, connection):
        self.connection = connection

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def execute(self, statement):
        if self.connection.error is not None:
            raise self.connection.error
        self.connection.statements.append(" ".join(statement.split()))


def test_initialize_database_creates_idempotent_game_server_schema(monkeypatch):
    first_connection = RecordingSchemaConnection()
    second_connection = RecordingSchemaConnection()
    pending_connections = [first_connection, second_connection]
    monkeypatch.setattr(
        db_module,
        "get_connection",
        lambda: pending_connections.pop(0),
    )

    db_module.initialize_database()
    db_module.initialize_database()

    for connection in (first_connection, second_connection):
        assert connection.commit_count == 1
        assert connection.closed
        assert len(connection.statements) == 1
        statement = connection.statements[0]
        assert "CREATE TABLE IF NOT EXISTS game_server" in statement
        assert "server_id CHAR(36) PRIMARY KEY" in statement
        assert "host VARCHAR(255) NOT NULL" in statement
        assert "port SMALLINT UNSIGNED NOT NULL" in statement
        assert "created_at DATETIME(6) NOT NULL" in statement
        assert "last_heartbeat DATETIME(6) NOT NULL" in statement
        assert "INDEX idx_game_server_last_heartbeat (last_heartbeat)" in statement


def test_initialize_database_closes_connection_when_schema_creation_fails(monkeypatch):
    connection = RecordingSchemaConnection(error=RuntimeError("schema failure"))
    monkeypatch.setattr(db_module, "get_connection", lambda: connection)

    with pytest.raises(RuntimeError, match="schema failure"):
        db_module.initialize_database()

    assert connection.commit_count == 0
    assert connection.closed


def test_database_error_returns_500_and_closes_connection(monkeypatch):
    connection = RecordingSchemaConnection(error=RuntimeError("database unavailable"))
    monkeypatch.setattr(main_module, "get_connection", lambda: connection)
    monkeypatch.setattr(main_module, "initialize_database", lambda: None, raising=False)

    with TestClient(main_module.app, raise_server_exceptions=False) as test_client:
        response = test_client.post(
            "/game-servers/register",
            json={
                "server_id": "77777777-7777-4777-8777-777777777777",
                "port": 7777,
            },
        )

    assert response.status_code == 500
    assert connection.commit_count == 0
    assert connection.closed
