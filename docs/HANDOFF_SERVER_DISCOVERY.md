# 게임 서버 등록·검색 기능 인수인계

이 문서는 이전 대화 기록이 전혀 없는 Codex가 작업을 이어받기 위한 단일 진입점이다. 새 컴퓨터에서는 저장소를 받은 뒤 이 문서와 루트의 `CLAUDE.md`를 먼저 읽고, 실제 코드를 다시 확인한 후 작업한다.

## 1. 저장소와 현재 단계

- 저장소: `https://github.com/pipeCHeck/L20260713_Day03.git`
- 기본 브랜치: `main`
- 프로젝트: Unreal Engine C++ 프로젝트 `L20260713_Day03`
- 웹 백엔드: `Server/`의 FastAPI + PyMySQL
- 현재 단계: 백엔드 계약 구현 및 mock DB 자동 테스트 완료
- 아직 완료되지 않은 검증: 실제 MySQL/MariaDB를 사용한 DB·HTTP 통합 검증
- Unreal 게임 서버 등록, heartbeat, 로그인 응답 파싱, 게임 서버 접속 구현은 아직 시작하지 않았다.

작업 범위의 최종 목표는 다음과 같다.

1. 기존 Title 화면에서 사용자가 웹서버 주소, ID, 비밀번호를 입력하고 로그인한다.
2. 기존 StartServer 버튼으로 `Lobby?Listen`을 열면 Listen Server가 FastAPI에 자신의 주소와 포트를 등록한다.
3. Listen Server는 주기적으로 heartbeat를 보내고, FastAPI는 TTL이 지난 서버를 접속 가능 목록에서 제외한다.
4. 다른 클라이언트의 로그인 응답에 접속 가능한 게임 서버 주소가 포함된다.
5. 기존 ConnectServer 버튼이 응답으로 받은 `host:port`를 사용해 게임 서버에 접속한다.

## 2. 반드시 보존할 기존 구조

새 로그인 레벨, 새 로그인 위젯, 새 GameInstance, 중복 HTTP Subsystem을 만들지 않는다. 기존 구조를 확장한다.

주요 파일:

- `Content/Maps/Title.umap`
- `Content/Blueprints/Title/UI/WBP_Title.uasset`
- `Source/L20260713_Day03/Title/TitlePC.*`
- `Source/L20260713_Day03/Title/TitleWidgetBase.*`
- `Source/L20260713_Day03/Web/WebApiSubsystem.*`
- `Source/L20260713_Day03/DataGameInstanceSubsystem.*`
- `Source/L20260713_Day03/Lobby/LobbyGM.*`
- `Server/main.py`
- `Server/db.py`
- `Server/tests/test_backend.py`

현재 Unreal 흐름:

1. `ATitlePC::BeginPlay()`가 기존 `WBP_Title`을 만든다.
2. `UTitleWidgetBase::Login()`이 `UWebApiSubsystem::RequestLogin()`을 호출한다.
3. 로그인 성공 데이터는 `UDataGameInstanceSubsystem`에 저장된다.
4. `UTitleWidgetBase::StartServer()`는 `OpenLevel("Lobby", ..., "Listen")`을 호출한다.
5. `UTitleWidgetBase::ConnectServer()`는 현재 `ServerIP` 입력값으로 접속한다.
6. `ALobbyGM::StartPlay()`는 현재 `Super::StartPlay()`만 호출한다.

주소의 역할은 반드시 분리한다.

- 기존 `ServerIP` 위젯 및 `UDataGameInstanceSubsystem::ServerIP`: FastAPI 웹서버 주소
- 새 `GameServerHost`와 `GameServerPort`: 로그인 응답으로 받은 Unreal 게임 서버 주소
- `ConnectServer()`는 최종적으로 `ServerIP` 입력창이 아니라 저장된 게임 서버 주소를 사용해야 한다.

## 3. 완료된 백엔드 구현

변경된 백엔드 파일:

- `Server/db.py`
- `Server/main.py`
- `Server/requirements.txt`
- `Server/tests/test_backend.py`

FastAPI lifespan에서 `initialize_database()`가 실행되며 다음 테이블을 멱등적으로 생성한다.

```sql
CREATE TABLE IF NOT EXISTS game_server (
    server_id CHAR(36) PRIMARY KEY,
    host VARCHAR(255) NOT NULL,
    port SMALLINT UNSIGNED NOT NULL,
    created_at DATETIME(6) NOT NULL,
    last_heartbeat DATETIME(6) NOT NULL,
    INDEX idx_game_server_last_heartbeat (last_heartbeat)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

상수:

- heartbeat 권장 간격: 10초
- 서버 유효 TTL: 30초
- TTL 판정 기준: 애플리케이션 시간이 아니라 MySQL `UTC_TIMESTAMP(6)`

여러 유효 서버가 있으면 다음 순서로 한 개만 선택한다.

```sql
ORDER BY last_heartbeat DESC, server_id ASC
LIMIT 1
```

### 구현된 API 계약

#### `POST /game-servers/register`

요청:

```json
{
  "server_id": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
  "port": 7777
}
```

- `server_id`는 UUID로 검증한다.
- `port`는 1부터 65535까지 허용한다.
- `host`는 JSON에서 받지 않고 FastAPI의 `request.client.host`를 사용한다.
- 같은 `server_id`는 `ON DUPLICATE KEY UPDATE`로 host, port, heartbeat 시각을 갱신한다.
- 등록 시 만료 서버를 기회적으로 삭제한다.

성공 응답:

```json
{
  "result": true,
  "message": "",
  "server_id": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
  "host": "192.168.0.25",
  "port": 7777,
  "heartbeat_interval_seconds": 10,
  "ttl_seconds": 30
}
```

#### `POST /game-servers/heartbeat`

요청:

```json
{
  "server_id": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
}
```

성공 응답:

```json
{
  "result": true,
  "message": ""
}
```

등록되지 않은 ID는 HTTP 404와 다음 본문을 반환한다.

```json
{
  "detail": "등록되지 않은 게임 서버입니다"
}
```

#### `POST /game-servers/unregister`

요청은 heartbeat와 같은 `server_id` 구조다. 서버가 이미 없어도 HTTP 200으로 처리하는 멱등 API다.

#### `POST /login`

기존 인증 요청은 유지한다.

```json
{
  "user_id": "tester",
  "passwd": "1234"
}
```

로그인 성공 및 유효 서버 존재:

```json
{
  "result": true,
  "message": "",
  "idx": 10,
  "nickname": "tester",
  "level": 1,
  "game_server": {
    "server_id": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
    "host": "192.168.0.25",
    "port": 7777
  }
}
```

로그인 성공 및 유효 서버 없음:

```json
{
  "result": true,
  "message": "",
  "idx": 10,
  "nickname": "tester",
  "level": 1,
  "game_server": null
}
```

로그인 실패에도 `game_server`는 `null`이다. 기존 `/signup` 응답 계약은 변경하지 않았다.

## 4. 검증 상태

인수인계 문서 작성 시 다음 명령을 다시 실행했다.

```powershell
Server\.venv\Scripts\python.exe -m pytest Server\tests -q
Server\.venv\Scripts\python.exe -m py_compile Server\db.py Server\main.py Server\tests\test_backend.py
Server\.venv\Scripts\python.exe -m pip check
git diff --check
```

관찰된 결과:

- `17 passed`
- Python 컴파일 검사 성공
- `pip check`: 깨진 의존성 없음
- 테스트는 실제 MySQL이 아니라 `FakeDatabase`를 사용한 API/SQL 계약 테스트다.

아직 확인되지 않은 항목:

- 실제 MySQL에서 DDL을 반복 실행했을 때의 결과
- 실제 MySQL의 `ON DUPLICATE KEY UPDATE`
- 실제 `UTC_TIMESTAMP(6)` heartbeat 갱신
- 실제 `member` 테이블을 사용하는 회원가입·로그인 회귀 테스트
- 실제 uvicorn HTTP 요청

이전 컴퓨터에서는 MySQL/MariaDB 서비스와 3306 리스너가 없어 실제 통합 검증이 불가능했다.

## 5. 새 컴퓨터에서 시작하는 방법

현재 `git lfs ls-files` 결과는 비어 있으며 기존 `.uasset`은 일반 Git 객체로 저장되어 있다. 따라서 새 컴퓨터에서는 우선 일반 clone만 수행한다. 작업 도중 임의로 LFS 마이그레이션이나 이력 재작성을 하지 않는다.

```powershell
git clone https://github.com/pipeCHeck/L20260713_Day03.git
cd L20260713_Day03
git status
git lfs ls-files
```

Python 로컬 가상환경은 저장소에서 전달하지 않는다. 새 컴퓨터에서 다시 만든다.

```powershell
py -3.12 -m venv Server\.venv
Server\.venv\Scripts\python.exe -m pip install -r Server\requirements.txt
Server\.venv\Scripts\python.exe -m pytest Server\tests -q
```

실제 DB 검증 전 준비:

1. MySQL 또는 MariaDB를 실행한다.
2. 현재 `Server/db.py`가 기대하는 데이터베이스와 `member` 테이블을 준비한다.
3. 로컬 DB 접속 설정을 확인한다. 민감한 값을 로그나 문서에 복사하지 않는다.
4. 현재 저장소의 DB 비밀번호가 실제로 사용하는 비밀이라면 회전하고, 별도 작업으로 환경변수 이전을 고려한다.

FastAPI 실행:

```powershell
Set-Location Server
.venv\Scripts\python.exe -m uvicorn main:app --host 0.0.0.0 --port 8080
```

API를 실제 MySQL로 검증한 뒤 테스트용 member 및 game_server 행은 명시적으로 식별해서 정리한다. 사용자 데이터 전체 삭제나 테이블 초기화는 하지 않는다.

## 6. 다음 구현 순서

### 단계 A: 실제 MySQL 통합 검증

먼저 현재 백엔드를 수정하지 않고 실제 MySQL에서 다음을 확인한다.

- lifespan DDL 성공 및 두 번째 실행의 멱등성
- 기존 `/signup`, `/login` 회귀 동작
- register → login 서버 반환 → heartbeat → unregister 전체 흐름
- TTL 30초 경과 서버 제외

실제 DB에서 실패가 발생하면 원인을 재현하는 테스트를 추가한 후 최소 수정한다.

### 단계 B: `UDataGameInstanceSubsystem` 확장

예정 필드:

- `FString GameServerHost`
- `int32 GameServerPort`
- `FString GameServerId`
- `bool bHasGameServer`

예정 보조 함수:

- `ClearGameServer()`
- `SetGameServer(...)`
- `HasValidGameServer()`
- `GetGameServerAddress()`

로그인 요청 전에 과거 서버 정보를 지워 stale 주소가 재사용되지 않게 한다. 기존 `ServerIP` 필드의 의미는 FastAPI host로 유지한다.

### 단계 C: 로그인 응답 파싱

기존 `UWebApiSubsystem::HandleAuthResponse()`를 확장한다.

- 로그인 성공과 `game_server` 존재 여부를 분리한다.
- `game_server: null`은 로그인 성공으로 유지한다.
- 객체가 있으면 `server_id`, `host`, `port`를 검증하고 Data Subsystem에 저장한다.
- 잘못된 선택적 서버 객체는 로그인 자체를 실패시키지 말고 “접속 가능한 서버 없음”으로 처리한다.
- 회원가입 파서는 기존 계약을 유지한다.

### 단계 D: Listen Server 등록 상태 머신

기존 `UWebApiSubsystem`에 추가한다.

- 프로세스 실행 세션별 UUID 생성
- register 요청
- 등록 성공 후 서버가 반환한 간격을 사용한 heartbeat
- heartbeat 404 시 재등록
- 요청 중첩 방지
- 현재 월드가 더 이상 `NM_ListenServer`가 아니면 heartbeat 중단
- GameInstance 종료 시 best-effort unregister
- HTTP 실패가 Listen Server 실행을 중단시키지 않음

`ALobbyGM::StartPlay()`에서 `Super::StartPlay()` 이후 `GetNetMode() == NM_ListenServer`와 실제 NetDriver 포트를 확인한 뒤 등록 시작을 요청한다. 포트 7777을 무조건 하드코딩하지 않는다.

### 단계 E: 기존 Title UI 접속 흐름 변경

- 로그인 성공이면 StartServer 버튼 활성화
- 로그인 성공이고 유효 서버가 있을 때만 ConnectServer 버튼 활성화
- 서버가 없으면 `InfoText`로 안내
- `ConnectServer()`는 Data Subsystem의 `host:port`를 사용
- 중복 접속 시도 방지
- 새 레벨·위젯을 만들지 않음
- `WBP_Title.uasset`은 필수 변경 대상이 아님

### 단계 F: 빌드와 다중 실행 검증

- Unreal C++ 빌드
- 호스트: Title 로그인 → StartServer → Lobby Listen → register/heartbeat 확인
- 클라이언트: Title 로그인 → 서버 정보 수신 → ConnectServer → Lobby 접속
- 호스트 강제 종료 → TTL 이후 로그인 응답에서 서버 제외
- FastAPI 일시 중단 후 재시작 → Listen Server 재등록 확인

## 7. 네트워크 전제와 알려진 제한

- 현재 구현은 LAN 최소 버전이다.
- register의 host는 `request.client.host`다.
- 호스트가 FastAPI에 `127.0.0.1`로 요청하면 다른 PC에 반환할 수 없는 loopback 주소가 등록된다. LAN 테스트에서는 호스트가 FastAPI의 LAN 주소로 요청해야 한다.
- NAT, 포트포워딩, STUN, 프록시의 `X-Forwarded-For`, 서버 등록 인증 토큰은 현재 범위에 없다.
- 서버 목록 UI나 로드밸런싱은 구현하지 않는다.
- 현재 인증 코드는 비밀번호를 평문 비교한다. 이번 서버 검색 기능과 별개의 보안 부채이며, 요청 없이 인접 리팩터링하지 않는다.

## 8. 작업 규칙

- 루트 `CLAUDE.md`를 따른다.
- 구현 전에 현재 파일과 Git 상태를 다시 확인한다.
- 관련 없는 파일을 수정하거나 포맷하지 않는다.
- 테스트를 먼저 추가하거나 실패를 재현한 뒤 구현한다.
- 자동 검증과 수동 검증을 구분한다.
- Unreal 에디터에서만 가능한 검증을 성공했다고 추측하지 않는다.
- 실제 DB 통합 검증 전에는 백엔드 단계를 완전히 완료됐다고 판정하지 않는다.
- 각 단계가 빌드·테스트된 뒤 다음 단계로 넘어간다.

## 9. 새 Codex 대화에 넣을 시작 프롬프트

아래 프롬프트를 저장소를 연 새 Codex 대화에 그대로 붙여 넣는다.

```text
이 저장소의 docs/HANDOFF_SERVER_DISCOVERY.md와 루트 CLAUDE.md를 처음부터 끝까지 읽어줘. 이전 대화 기록은 없다고 가정하고, 문서의 내용도 맹신하지 말고 현재 Git 상태와 관련 소스 파일을 대조해줘.

우선 다음 작업만 수행해줘.

1. 현재 main 브랜치와 원격 상태를 확인한다.
2. Server 백엔드 자동 테스트를 새로 실행한다.
3. 실제 MySQL/MariaDB가 사용 가능한지 확인한다.
4. 사용 가능하면 기존 사용자 데이터를 훼손하지 않는 임시 데이터로 register, heartbeat, unregister, login, TTL 통합 검증을 수행한다.
5. 실제 DB 통합 검증이 성공한 경우에만 백엔드 단계를 완료로 판정한다.
6. 그 다음 Unreal 구현 단계 B~F의 세부 설계를 현재 코드 기준으로 다시 확인해 짧게 제시한다.

아직 Unreal C++ 코드를 수정하지 마. 통합 검증 결과와 Unreal 변경 설계를 먼저 보고하고 내 승인을 기다려줘.

중요 제약:
- 기존 Title 레벨과 WBP_Title UI를 재사용한다.
- 기존 ServerIP는 FastAPI 주소다.
- 게임 서버 주소는 별도 GameServerHost/GameServerPort에 저장한다.
- StartServer는 기존 Lobby Listen 흐름을 유지한다.
- 새 로그인 레벨, 새 로그인 위젯, 새 GameInstance, 중복 HTTP Subsystem을 만들지 않는다.
- 관련 없는 리팩터링을 하지 않는다.
```

## 10. 완료 조건

전체 기능은 다음 조건이 모두 충족되어야 완료다.

- mock DB 백엔드 테스트 통과
- 실제 MySQL 통합 검증 통과
- Unreal C++ 빌드 통과
- 기존 로그인/회원가입 회귀 확인
- Listen Server 등록과 heartbeat 확인
- 로그인 응답의 서버 주소 파싱 확인
- 기존 ConnectServer 버튼을 통한 실제 접속 확인
- 서버 종료 후 TTL 만료 확인
- 자동으로 확인하지 못한 에디터/다중 PC 단계가 수동 체크리스트에 기록됨
