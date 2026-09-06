from contextlib import asynccontextmanager
from uuid import UUID

from fastapi import FastAPI, HTTPException, Request
from pydantic import BaseModel, Field
import pymysql

from db import (
    GAME_SERVER_HEARTBEAT_INTERVAL_SECONDS,
    GAME_SERVER_TTL_SECONDS,
    get_connection,
    initialize_database,
)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    initialize_database()
    yield


app = FastAPI(title="L20260713_Day03 Auth Server", lifespan=lifespan)


class AuthRequest(BaseModel):
    user_id: str = Field(min_length=1)
    passwd: str = Field(min_length=1)


class AuthResponse(BaseModel):
    result: bool
    message: str = ""
    idx: int = 0
    nickname: str = ""
    level: int = 0


class GameServerInfo(BaseModel):
    server_id: UUID
    host: str
    port: int


class LoginResponse(AuthResponse):
    game_server: GameServerInfo | None = None


class GameServerIdentityRequest(BaseModel):
    server_id: UUID


class GameServerRegisterRequest(GameServerIdentityRequest):
    port: int = Field(ge=1, le=65535)


class RegistryResponse(BaseModel):
    result: bool
    message: str = ""


class GameServerRegisterResponse(RegistryResponse):
    server_id: UUID
    host: str
    port: int
    heartbeat_interval_seconds: int
    ttl_seconds: int


def delete_expired_game_servers(cur):
    cur.execute(
        f"DELETE FROM game_server"
        f" WHERE last_heartbeat < UTC_TIMESTAMP(6)"
        f" - INTERVAL {GAME_SERVER_TTL_SECONDS} SECOND"
    )


def find_available_game_server(cur):
    cur.execute(
        "SELECT server_id, host, port FROM game_server"
        f" WHERE last_heartbeat >= UTC_TIMESTAMP(6)"
        f" - INTERVAL {GAME_SERVER_TTL_SECONDS} SECOND"
        " ORDER BY last_heartbeat DESC, server_id ASC"
        " LIMIT 1"
    )
    return cur.fetchone()


@app.post("/signup", response_model=AuthResponse)
def signup(req: AuthRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            try:
                cur.execute(
                    "INSERT INTO member (user_id, passwd, nickname, level)"
                    " VALUES (%s, %s, %s, 1)",
                    (req.user_id, req.passwd, req.user_id),
                )
            except pymysql.err.IntegrityError:
                return AuthResponse(result=False, message="이미 존재하는 아이디입니다")

            new_idx = cur.lastrowid

        conn.commit()
    finally:
        conn.close()

    return AuthResponse(
        result=True, idx=new_idx, nickname=req.user_id, level=1
    )


@app.post("/login", response_model=LoginResponse)
def login(req: AuthRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT idx, nickname, level FROM member"
                " WHERE user_id = %s AND passwd = %s",
                (req.user_id, req.passwd),
            )
            row = cur.fetchone()
            game_server_row = find_available_game_server(cur) if row else None
    finally:
        conn.close()

    if row is None:
        return LoginResponse(
            result=False, message="아이디 또는 비밀번호가 올바르지 않습니다"
        )

    game_server = (
        GameServerInfo(**game_server_row) if game_server_row is not None else None
    )
    return LoginResponse(
        result=True,
        idx=row["idx"],
        nickname=row["nickname"],
        level=row["level"],
        game_server=game_server,
    )


@app.post("/game-servers/register", response_model=GameServerRegisterResponse)
def register_game_server(req: GameServerRegisterRequest, request: Request):
    if request.client is None or not request.client.host:
        raise HTTPException(status_code=400, detail="요청자 주소를 확인할 수 없습니다")

    server_id = str(req.server_id)
    host = request.client.host
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            delete_expired_game_servers(cur)
            cur.execute(
                "INSERT INTO game_server"
                " (server_id, host, port, created_at, last_heartbeat)"
                " VALUES (%s, %s, %s, UTC_TIMESTAMP(6), UTC_TIMESTAMP(6))"
                " ON DUPLICATE KEY UPDATE"
                " host = VALUES(host),"
                " port = VALUES(port),"
                " last_heartbeat = UTC_TIMESTAMP(6)",
                (server_id, host, req.port),
            )
        conn.commit()
    finally:
        conn.close()

    return GameServerRegisterResponse(
        result=True,
        server_id=req.server_id,
        host=host,
        port=req.port,
        heartbeat_interval_seconds=GAME_SERVER_HEARTBEAT_INTERVAL_SECONDS,
        ttl_seconds=GAME_SERVER_TTL_SECONDS,
    )


@app.post("/game-servers/heartbeat", response_model=RegistryResponse)
def heartbeat_game_server(req: GameServerIdentityRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "UPDATE game_server"
                " SET last_heartbeat = UTC_TIMESTAMP(6)"
                " WHERE server_id = %s",
                (str(req.server_id),),
            )
            if cur.rowcount == 0:
                cur.execute(
                    "SELECT 1 FROM game_server WHERE server_id = %s",
                    (str(req.server_id),),
                )
                if cur.fetchone() is None:
                    raise HTTPException(
                        status_code=404,
                        detail="등록되지 않은 게임 서버입니다",
                    )
        conn.commit()
    finally:
        conn.close()

    return RegistryResponse(result=True)


@app.post("/game-servers/unregister", response_model=RegistryResponse)
def unregister_game_server(req: GameServerIdentityRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "DELETE FROM game_server WHERE server_id = %s",
                (str(req.server_id),),
            )
        conn.commit()
    finally:
        conn.close()

    return RegistryResponse(result=True)
