# Unreal Login Server

Unreal Engine과 FastAPI를 연동하여 로그인 기반 게임 서버 검색 및 접속 기능을 구현한 프로젝트입니다. 기존 로그인/회원가입 시스템을 확장하고, AI 기반 개발 도구인 Codex를 활용하여 서버 등록부터 클라이언트 접속까지 단계적으로 구현했습니다.

## 프로젝트 목표

기존 Unreal 프로젝트에 다음 두 기능을 추가하는 것을 목표로 했습니다.

1. Unreal Listen Server가 시작되면 웹서버에 자신의 IP와 Port를 등록합니다.
2. 클라이언트가 로그인하면 웹서버에서 접속 가능한 게임 서버 정보를 받아 해당 서버로 접속합니다.

## 주요 기능

* **로그인 및 회원가입**: FastAPI와 MySQL을 이용한 사용자 인증
* **게임 서버 자동 등록**: Listen Server 시작 시 실제 바인딩된 Port와 서버 UUID를 웹서버에 등록
* **Heartbeat**: 실행 중인 서버의 등록 상태를 주기적으로 갱신
* **자동 재등록**: 등록 정보가 사라진 경우 동일 UUID로 재등록
* **로그인 기반 서버 검색**: 로그인 응답에서 접속 가능한 게임 서버의 Host와 Port 수신
* **게임 서버 접속**: 검색된 `Host:Port`를 이용해 Unreal Listen Server로 접속
* **서버 종료 정리**: 정상 종료 시 unregister를 시도하고, 비정상 종료 시 TTL로 만료 서버 제외

## 시스템 흐름

```text
Unreal Host
    ↓
Lobby Listen Server 시작
    ↓
FastAPI에 서버 등록
    ↓
MySQL에 서버 정보 저장
    ↓
Heartbeat로 등록 유지

Unreal Client
    ↓
로그인 요청
    ↓
FastAPI에서 접속 가능한 서버 조회
    ↓
Host / Port 수신
    ↓
Unreal 게임 서버 접속
```

## 개발 환경

* Unreal Engine 5.8
* C++
* Python 3.12
* FastAPI
* MySQL 8.4
* Git / GitHub
* OpenAI Codex

## AI 활용 및 개발 과정

Codex를 활용하여 기존 프로젝트 구조를 분석하고, 기능을 작은 단계로 나누어 구현했습니다. 각 단계에서 테스트를 먼저 작성하고 RED → 구현 → GREEN 검증을 수행했으며, 결과를 확인한 뒤 Git 커밋으로 작업을 관리했습니다.

게임 서버 정보 저장, 로그인 응답 파싱, 서버 등록, heartbeat, 접속, unregister까지 단계적으로 확장했습니다.

## 검증 현황

Codex 실행 결과 기준으로 Unreal Automation Test **26개**와 백엔드 테스트 **19개**가 통과했습니다. 실제 MySQL과 FastAPI를 이용한 HTTP 통합 검증에서도 로그인, 서버 등록·조회, heartbeat, unregister, TTL 동작을 확인했습니다.

실제 Unreal Host와 별도 Client를 이용한 최종 접속 시연은 별도 통합 검증 단계로 진행합니다.

## 상세 문서

구현 구조와 AI 활용 과정은 [개발 보고서](docs/IMPLEMENTATION_REPORT.md)를 참고해 주세요.

## 저장소

https://github.com/pipeCHeck/ueGI8_LoginServer
