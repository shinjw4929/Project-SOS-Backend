# 스킬 사용 시나리오

## 스킬 계층 구조

```
[Tier 1: 오케스트레이션] 사용자 호출 → Tier 2 자동 호출
  implement ──auto──→ /sync-comments, /sync-docs
  plan-execute ──auto──→ /sync-comments, /sync-docs
  diagnose ──auto──→ /sync-comments
  plan-create ──auto──→ /review-plan ──auto──→ /plan-edit

[Tier 2: 후처리] 자동 호출 OR 사용자 직접 호출
  sync-comments      주석 정합성 점검 및 수정
  sync-docs          docs 문서 동기화

[Tier 2: 검증] 텍스트 권장만 (사용자 직접 호출)
  build              CMake 빌드 + 에러 파싱
  test               CTest + Catch2 테스트
  review-code        3개 도메인 에이전트 병렬 리뷰 (안전성/네트워크/컨벤션)

[독립 도구] 사용자 직접 호출
  analyze            의존성/영향도 분석 (읽기 전용)
  commit             커밋 메시지 작성 및 커밋
  create-skill       기존 패턴 기반 새 스킬 생성 (200줄 제한)
  plan-edit          계획 부분 수정
  port-skill         Unity 스킬을 백엔드로 포팅
  review-design      시스템 설계 방향성 검증 (읽기 전용)
  review-plan        계획 검토
  review-skill       스킬 토큰 효율성/중복/적합성 점검 (읽기 전용)
```

## 공유 참조 문서

| 문서 | 참조 스킬 |
|------|-----------|
| `docs/Checklists/pattern-search-guide.md` | implement, plan-execute, diagnose |
| `docs/Checklists/review-code-checklist.md` | review-code, review-plan |
| `docs/Documentation-Checklist.md` | sync-docs |
| `docs/Plans/계획-작성-가이드.md` | plan-create, plan-edit, plan-execute |

---

## 시나리오별 스킬 선택

### 1. 새 기능 구현 (소규모, 단일 모듈)

```
/implement [구현 대상]
```

패턴 탐색 → 코드 작성 → /sync-comments(자동) → /sync-docs(자동)
필요 시 사용자가 /build, /test, /review-code 추가 실행.

**예시:**
- "Config에 새 accessor 추가"
- "Room Server에 새 Envelope 핸들러 추가"
- "ChannelManager에 시스템 메시지 기능 추가"

### 2. 새 기능 구현 (대규모, 여러 모듈에 걸친 변경)

```
/plan-create [기능명]          계획 생성 → /review-plan(자동)
/plan-execute [기능명]         Phase별 실행 → /sync-comments(자동) → /sync-docs(자동)
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
/diagnose [에러 메시지 또는 증상]
```

진단 → 수정 적용 → /sync-comments(자동)
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
/review-code [선택: 파일/커밋 범위/관점]
```

3개 도메인 에이전트(안전성/네트워크/컨벤션)가 워크트리 격리로 병렬 리뷰. `docs/Checklists/review-code-checklist.md` 기준.

### 7. 주석만 빠르게 정리

```
/sync-comments [선택: 파일 경로]
```

Tier 1 스킬이 자동 호출하므로, 단독 사용은 핫픽스 후 주석만 정리할 때.

### 8. 문서만 업데이트

```
/sync-docs [선택: 범위]
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

변경사항을 주제별로 분류하여 각각 독립 커밋으로 작성한다. 사용자 확인 후 실행.

### 12. Unity 스킬 포팅

```
/port-skill [Unity 스킬명]
```

Unity 프로젝트(Project-SOS)의 스킬을 읽어 C++ 백엔드 컨텍스트에 맞게 변환하여 작성한다.

**예시:**
- `/port-skill review-design` - Unity의 기획 검증 스킬을 백엔드용으로 포팅
- `/port-skill sync-comments` - Unity의 주석 검토 스킬을 백엔드용으로 포팅

### 13. 시스템 설계 검증

```
/review-design [선택: 플랜 경로/기능 설명]
```

코드 변경이나 구현 계획이 docs/Systems/ 및 CLAUDE.md의 설계 방향성과 일치하는지 검증한다.

**예시:**
- `/review-design` - 현재 git diff를 설계 문서와 대조
- `/review-design 게임서버-중복세션-방지` - 특정 계획의 설계 정합성 검증

### 14. 스킬 검토

```
/review-skill [선택: 스킬명]
```

스킬 파일의 토큰 효율성, 기존 스킬 중복, 프로젝트 적합성을 점검한다. 인자 없이 호출하면 전체 스킬 스캔.

**예시:**
- `/review-skill implement` - implement 스킬 상세 검토
- `/review-skill` - 전체 스킬 토큰 효율성 스캔

### 15. 새 스킬 생성

```
/create-skill [스킬명 또는 목적]
```

기존 스킬 패턴을 분석하여 동일한 구조의 새 스킬을 생성한다. SKILLS.md 인덱스도 자동 업데이트.

**예시:**
- `/create-skill review-perf 성능 전문 리뷰`
- `/create-skill gen-test 모듈별 테스트 자동 생성`

---

## 일반적인 워크플로우 체이닝

### 소규모 작업

```
/implement → (/sync-comments + /sync-docs 자동) → /build → /test → /commit
```

### 대규모 작업

```
/plan-create → (/review-plan 자동)
/plan-execute → (/sync-comments + /sync-docs 자동) → /review-code → /build → /test → /commit
```

### 분석 후 구현

```
/analyze → /plan-create 또는 /implement
```

### 디버그 후 검증

```
/diagnose → (/sync-comments 자동) → /build → /test → /commit
```
