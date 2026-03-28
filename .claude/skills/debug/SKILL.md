---
name: debug
description: C++ 백엔드 서버의 에러, 비정상 동작, 크래시를 체계적으로 진단하고 수정합니다.
allowed-tools: Read, Edit, Grep, Glob, Bash, Agent, Skill
---

## 디버그 실행

### 1단계: 증상 수집

$ARGUMENTS에서 다음을 파악한다:
- **에러 메시지**: 로그 출력, 스택 트레이스, 에러 코드
- **동작 설명**: 예상 동작 vs 실제 동작
- **재현 조건**: 언제, 어떤 상황에서 발생하는가
- **발생 위치**: 어떤 서버/모듈에서 발생하는가

### 2단계: 에러 분류

증상을 6개 카테고리 중 하나로 분류한다:

| 카테고리 | 증상 패턴 | 탐색 시작점 |
|----------|----------|------------|
| **A. 메모리/RAII** | segfault, double free, use-after-free | shared_ptr/unique_ptr 수명, 소켓 닫힘 순서 |
| **B. 스레드/동시성** | data race, 간헐적 크래시, 교착 상태 | io_context 스레드 모델, strand 사용 여부 |
| **C. 네트워크 I/O** | 연결 끊김, 타임아웃, 메시지 유실 | async_read/write 에러 처리, Codec 버퍼 |
| **D. Protobuf 직렬화** | 파싱 실패, 필드 누락, 잘못된 dispatch | proto 정의 vs 코드 불일치, Envelope payload_case |
| **E. Redis 연결** | 토큰 검증 실패, Rate Limit 오작동, 키 미정리 | RedisClient 연결, TTL 설정, 키 패턴 |
| **F. Docker/인프라** | 컨테이너 시작 실패, 서비스 간 연결 불가 | docker-compose.yml, 환경변수, 포트 매핑 |

### 3단계: 근본 원인 추적

분류에 따라 탐색한다:

1. 에러 메시지/스택 트레이스에서 파일:라인 추출
2. 해당 코드와 호출 체인을 읽는다
3. `docs/Checklists/pattern-search-guide.md`를 참조하여 유사 패턴과 비교
4. `docs/Systems/` 관련 문서에서 정상 흐름을 확인
5. 정상 흐름과 실제 동작의 차이점을 특정

### 4단계: 진단 결과 출력

```
## 진단 결과

### 증상 요약
[에러/동작 한 줄 요약]

### 분류
[A~F 카테고리]

### 근본 원인
[원인 설명 + 코드 위치]

### 수정 방안
[구체적 수정 내용]
```

### 5단계: 수정 적용

사용자 확인 후 수정을 적용한다. 수정 시 `docs/Checklists/pattern-search-guide.md` 기준으로 기존 패턴을 따른다.

### 6단계: 후처리 (자동)

수정 완료 후 다음 스킬을 호출한다:
- `/review-comments` — 변경 파일의 주석 정합성 점검
