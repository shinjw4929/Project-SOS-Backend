# 스킬 사용 시나리오

## 스킬 계층 구조

```
[Tier 1: 오케스트레이션] 사용자 호출 → Tier 2 자동 호출
  implement ──auto──→ /review-comments, /update-docs
  plan-execute ──auto──→ /review-comments, /update-docs
  debug ──auto──→ /review-comments
  plan-create ──auto──→ /review-plan ──auto──→ /plan-edit

[Tier 2: 후처리] 자동 호출 OR 사용자 직접 호출
  review-comments    주석 정합성 점검 및 수정
  update-docs        docs 문서 동기화

[Tier 2: 검증] 텍스트 권장만 (사용자 직접 호출)
  build              CMake 빌드 + 에러 파싱
  test               CTest + Catch2 테스트
  review-code        코드 리뷰

[독립 도구] 사용자 직접 호출
  analyze            의존성/영향도 분석 (읽기 전용)
  commit             커밋 메시지 작성 및 커밋
  plan-edit          계획 부분 수정
  review-plan        계획 검토
```

## 공유 참조 문서

| 문서 | 참조 스킬 |
|------|-----------|
| `docs/Checklists/pattern-search-guide.md` | implement, plan-execute, debug |
| `docs/Checklists/review-code-checklist.md` | review-code, review-plan |
| `docs/Documentation-Checklist.md` | update-docs |
| `docs/Plans/계획-작성-가이드.md` | plan-create, plan-edit, plan-execute |

---

## 시나리오별 스킬 선택

### 1. 새 기능 구현 (소규모, 단일 모듈)

```
/implement [구현 대상]
```

패턴 탐색 → 코드 작성 → /review-comments(자동) → /update-docs(자동)
필요 시 사용자가 /build, /test, /review-code 추가 실행.

**예시:**
- "Config에 새 accessor 추가"
- "Room Server에 새 Envelope 핸들러 추가"
- "ChannelManager에 시스템 메시지 기능 추가"

### 2. 새 기능 구현 (대규모, 여러 모듈에 걸친 변경)

```
/plan-create [기능명]          계획 생성 → /review-plan(자동)
/plan-execute [기능명]         Phase별 실행 → /review-comments(자동) → /update-docs(자동)
```

필요 시 사용자가 /build, /test, /review-code, /commit 추가 실행.

**예시:**
- "매치메이킹 시스템 구현"
- "게임 서버 상태 모니터링 추가"
- "새 내부 채널 프로토콜 추가"

### 3. 기존 계획 수정 후 이어서 실행

```
/plan-edit [기능명]            미실행 Phase 수정
/plan-execute [기능명]         수정된 계획 이어서 실행
```

### 4. 버그 수정

```
/debug [에러 메시지 또는 증상]
```

진단 → 수정 적용 → /review-comments(자동)
필요 시 사용자가 /build, /test 추가 실행.

**예시:**
- "클라이언트 연결 시 segfault 발생"
- "Room에서 게임 시작 후 토큰 검증 실패"
- "Chat 세션 종료 시 Redis 키 미정리"

### 5. 코드 변경 전 영향 분석

```
/analyze [모듈/클래스/Redis 키/포트]
```

읽기 전용. 코드를 수정하지 않고 의존성 그래프와 영향 범위만 출력한다.

**예시:**
- "SessionStore에 새 메서드 추가하면 어디가 영향받는지"
- "Redis 키 패턴 변경 시 영향 범위"
- "포트 8081을 변경하면 어디를 수정해야 하는지"

### 6. 구현 후 코드 리뷰

```
/review-code [선택: 파일/커밋 범위]
```

`docs/Checklists/review-code-checklist.md` 기준으로 변경 코드를 검토한다.

### 7. 주석만 빠르게 정리

```
/review-comments [선택: 파일 경로]
```

Tier 1 스킬이 자동 호출하므로, 단독 사용은 핫픽스 후 주석만 정리할 때.

### 8. 문서만 업데이트

```
/update-docs [선택: 범위]
```

`docs/Documentation-Checklist.md` 매핑 테이블 기준으로 문서를 갱신한다.
Tier 1 스킬이 자동 호출하므로, 단독 사용은 수동으로 코드를 수정한 후 문서만 맞출 때.

### 9. 빌드 검증

```
/build [선택: 타겟]
```

Developer Command Prompt for VS 2022 필요. CMake configure + build.

### 10. 테스트 실행

```
/test [선택: 필터]
```

Developer Command Prompt for VS 2022 필요. CTest + Catch2.

### 11. 커밋

```
/commit
```

변경사항을 분석하여 한국어 커밋 메시지를 작성하고, 사용자 확인 후 커밋한다.

---

## 일반적인 워크플로우 체이닝

### 소규모 작업

```
/implement → (/review-comments + /update-docs 자동) → /build → /test → /commit
```

### 대규모 작업

```
/plan-create → (/review-plan 자동)
/plan-execute → (/review-comments + /update-docs 자동) → /review-code → /build → /test → /commit
```

### 분석 후 구현

```
/analyze → /plan-create 또는 /implement
```

### 디버그 후 검증

```
/debug → (/review-comments 자동) → /build → /test → /commit
```
