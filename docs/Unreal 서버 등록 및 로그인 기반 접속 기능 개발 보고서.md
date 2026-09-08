# Unreal 서버 등록 및 로그인 기반 접속 기능 개발 보고서

## 1. 프로젝트 개요

### 개발 목적

기존 Unreal Engine 프로젝트의 로그인 시스템에 웹서버 기반 게임 서버 검색 기능을 추가했습니다. Unreal 서버가 실행되면 자신의 접속 정보를 웹서버에 등록하고, 클라이언트는 로그인 과정에서 해당 정보를 받아 게임 서버에 접속할 수 있도록 구현했습니다.

본 프로젝트는 **2026년 9월 9일까지 AI를 활용한 바이브 코딩으로 기능을 구현하는 과제**를 목적으로 진행했습니다.

### 주요 요구사항

- Unreal 서버 시작 시 웹서버에 서버 IP 등록
- 클라이언트 로그인 시 등록된 서버 IP 수신 및 접속
- AI를 활용한 구현 및 개발 문서 작성
- GitHub 저장소를 통한 결과물 제출

## 2. 기존 프로젝트 분석

기존 프로젝트에는 Unreal 로그인/회원가입 UI와 FastAPI, MySQL 기반 인증 시스템이 구성되어 있었습니다. 서버 등록 API와 로그인 시 게임 서버 정보를 반환하는 백엔드 기능도 일부 준비되어 있었으나, Unreal 클라이언트와 실제 Listen Server에 연결하는 부분은 구현되지 않은 상태였습니다.

따라서 기존 시스템을 새로 만드는 대신, 다음 구조를 유지하며 필요한 기능을 확장했습니다.

| 구성 요소                        | 역할                            |
| ---------------------------- | ----------------------------- |
| `UDataGameInstanceSubsystem` | 로그인 정보 및 검색된 게임 서버 정보 보관      |
| `UWebApiSubsystem`           | FastAPI HTTP 통신 및 서버 등록 상태 관리 |
| `ALobbyGM`                   | Listen Server 시작 시 등록 요청 연결   |
| `UTitleWidgetBase`           | 로그인 UI 및 게임 서버 접속             |
| FastAPI                      | 인증, 서버 등록·조회·유지·해제 API        |
| MySQL                        | 사용자 및 게임 서버 정보 저장             |

## 3. 시스템 설계

### 3.1 서버 등록 흐름

```text
StartServer()
    ↓
Lobby?Listen
    ↓
Listen Server 여부 확인
    ↓
활성 NetDriver에서 실제 Port 획득
    ↓
Hosting UUID 생성
    ↓
POST /game-servers/register
    ↓
MySQL game_server 저장
    ↓
주기적 heartbeat
```

게임 서버의 Port는 `7777`을 고정해서 사용하는 대신, 실제 NetDriver가 바인딩한 Port를 확인하도록 구현했습니다.

### 3.2 클라이언트 접속 흐름

```text
로그인 요청
    ↓
POST /login
    ↓
game_server 객체 수신
    ↓
GameServerHost / Port / Id 저장
    ↓
ConnectServer()
    ↓
Host:Port로 OpenLevel
```

기존 `ServerIP`는 FastAPI 웹서버 주소로 유지하고, 게임 서버 접속 주소는 별도 필드로 분리했습니다. 이를 통해 웹서버 주소와 Unreal 게임 서버 주소가 혼동되지 않도록 했습니다.

### 3.3 서버 상태 유지

서버 등록 이후에는 웹서버가 반환한 heartbeat 주기에 맞춰 등록 상태를 갱신합니다. 등록 정보가 존재하지 않아 heartbeat에서 HTTP 404가 반환되면, 기존 Hosting UUID를 유지한 채 재등록을 시도합니다.

Heartbeat는 특정 Lobby World에 종속되지 않도록 GameInstance 수명의 TimerManager를 사용했습니다. 따라서 Lobby에서 다른 게임 맵으로 ServerTravel이 발생하더라도 등록 유지 기능이 지속되도록 설계했습니다.

정상 종료 시에는 unregister를 best-effort로 전송하며, 비정상 종료 등으로 해제 요청이 전달되지 않은 경우에는 백엔드의 TTL을 통해 만료된 서버를 검색 결과에서 제외합니다.

## 4. AI 활용 개발 과정

OpenAI Codex를 사용하여 기존 코드를 분석하고, 기능별 구현 범위를 나눈 뒤 단계적으로 개발했습니다. 한 번에 전체 기능을 생성하기보다는 **작은 기능 구현 → 테스트 → 결과 검토 → Git 커밋** 순서로 진행했습니다.

| 단계       | 구현 내용                                   |
| -------- | --------------------------------------- |
| Phase 01 | 게임 서버 Host / Port / Id 저장 및 유효성 검사      |
| Phase 02 | 로그인 응답의 `game_server` 파싱 및 이전 서버 정보 초기화 |
| Phase 03 | Listen Server 시작 시 웹서버 등록 및 실제 Port 획득  |
| Phase 04 | Heartbeat 및 HTTP 404 발생 시 동일 UUID 재등록   |
| Phase 05 | 로그인으로 검색된 게임 서버 주소를 이용한 클라이언트 접속        |
| Phase 06 | Hosting Session 종료 및 unregister 처리      |
| Phase 07 | 실제 MySQL/FastAPI 연동과 HTTP 계약 통합 검증      |

각 구현 단계에서 Codex가 변경 파일과 테스트 결과를 보고하도록 하고, 이를 검토한 뒤 커밋하는 방식으로 작업했습니다. 이를 통해 기존 로그인/회원가입 기능을 유지하면서 서버 검색 기능을 점진적으로 추가했습니다.

## 5. 주요 구현 내용

### 게임 서버 정보 분리

`DataGameInstanceSubsystem`에 게임 서버 전용 Host, Port, Id를 추가하고, 유효성 검사와 주소 조합 함수를 구현했습니다. 잘못된 서버 정보가 전달되면 이전 값을 남기지 않고 전체 게임 서버 상태를 초기화하도록 했습니다.

### 로그인 응답 처리

FastAPI의 로그인 응답에 포함된 `game_server` 객체를 안전하게 파싱합니다. 서버 정보가 없거나 `null`인 경우에도 로그인 자체는 성공으로 처리하며, 접속 가능한 서버가 없다는 상태를 별도로 유지합니다.

### Listen Server 등록

`LobbyGM::StartPlay()`에서 Listen Server 여부를 확인하고 등록을 시작합니다. 서버 UUID와 등록 상태는 `WebApiSubsystem`에서 관리하여 Lobby의 수명과 분리했습니다.

### Heartbeat 및 재등록

등록 성공 후 주기적으로 heartbeat를 전송하고, HTTP 404가 발생하면 동일한 UUID를 사용해 재등록합니다. 일시적인 네트워크 오류와 등록 정보가 실제로 사라진 경우를 구분하여 처리합니다.

### 클라이언트 접속

기존에는 웹서버 주소 입력값을 게임 서버 접속 주소로 사용했으나, 이를 로그인으로 받은 `GameServerHost:GameServerPort`로 변경했습니다. 로그인은 성공했지만 접속 가능한 서버가 없는 경우에는 StartServer 버튼은 유지하고 ConnectServer 버튼만 비활성화합니다.

### 종료 처리

Hosting Session 종료 시 heartbeat와 진행 중인 등록 요청을 정리하고 unregister를 시도합니다. 늦게 도착한 HTTP 응답이 종료된 세션이나 새로운 세션의 상태를 변경하지 않도록 요청 및 세션을 구분해 처리했습니다.

## 6. 검증 결과

### 자동 테스트

Codex 실행 결과 기준으로 다음 검증을 통과했습니다.

| 검증                                  | 결과       |
| ----------------------------------- | -------- |
| Unreal Automation Test Phase 01\~06 | 26/26 성공 |
| 백엔드 테스트                             | 19/19 성공 |
| Python 문법 검사                        | 성공       |
| 패키지 의존성 검사                          | 성공       |
| Unreal Editor Win64 Development 빌드  | 성공       |

### 실제 FastAPI/MySQL 통합 검증

실제 MySQL에 연결한 FastAPI를 통해 다음 동작을 확인했습니다.

- 회원가입 및 로그인
- 서버 미등록 상태에서 로그인 성공과 `game_server: null` 반환
- 게임 서버 등록 및 등록 후 로그인에서 서버 정보 반환
- Heartbeat 정상 응답
- 미등록 UUID에 대한 HTTP 404
- Unregister 및 중복 unregister의 멱등성
- TTL 경과 후 만료 서버가 로그인 검색 결과에서 제외되는 동작

이 검증을 통해 **웹서버와 DB 사이의 서버 등록·조회·유지 계약이 실제 환경에서도 동작함을 확인했습니다.**

### 최종 시연 범위

실제 Unreal Host와 별도 Client를 실행하여 게임 서버에 접속하는 최종 멀티프로세스 시연은 별도 통합 검증 단계로 진행합니다. 자동 테스트와 HTTP 통합 검증 결과를 바탕으로, 최종 시연에서는 실제 네트워크 접속과 맵 이동을 확인합니다.

## 7. 개발 결과

기존 로그인 시스템에 Unreal 게임 서버 등록 및 검색 기능을 추가하여, **서버 시작 → 웹서버 등록 → 로그인 시 서버 조회 → 게임 서버 접속**으로 이어지는 전체 기능을 코드상 연결했습니다.

또한 heartbeat, 자동 재등록, 종료 시 unregister, TTL 기반 만료 처리 등을 추가하여 단순 IP 전달을 넘어 서버 상태를 유지하고 관리할 수 있는 구조로 확장했습니다.

AI를 활용한 단계별 구현과 테스트 중심의 작업 방식을 통해 기존 프로젝트의 구조를 유지하면서 요구 기능을 완성해 나갔으며, 실제 FastAPI/MySQL HTTP 통합 검증까지 진행했습니다.

## 8. GitHub

[https://github.com/pipeCHeck/ueGI8\_LoginServer](https://github.com/pipeCHeck/ueGI8_LoginServer)
