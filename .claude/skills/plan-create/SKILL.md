---
name: plan-create
description: 구현 계획을 docs/Plans/에 orchestration + Phase 파일로 생성합니다.
allowed-tools: Read, Grep, Glob, Bash, Write, Edit, Agent, EnterPlanMode, ExitPlanMode, Skill
---

## 계획 생성 실행

### 1단계: 템플릿 로드

`docs/Plans/계획-작성-가이드.md`를 읽어 계획 구조와 작성 규칙을 파악한다.

### 2단계: 요구사항 수집

$ARGUMENTS에서 다음 4가지를 파악한다 (명시되지 않은 항목은 사용자에게 질문):
- **변경 대상**: 어떤 모듈/서비스/파일을 변경하는가
- **현재 문제**: 지금 무엇이 부족하거나 잘못되어 있는가
- **원하는 결과**: 변경 후 어떤 상태가 되어야 하는가
- **제약 조건**: 기한, 호환성, 성능 요구사항

### 3단계: AS-IS 조사

Explore 에이전트를 활용하여 현재 상태를 파악한다:
- `docs/Systems/` — 관련 모듈 문서
- `docs/Plans/` — 기존 계획과의 관계
- 실제 코드 — 변경 대상 파일 구조, 의존성
- 영향 범위 — 변경이 미칠 다른 모듈

### 4단계: TO-BE 설계

AS-IS 기반으로 변경/추가/삭제 항목을 정의한다:
- 모듈 분리 (common/room/chat/infra)
- Protobuf 메시지 설계
- Redis 키 구조
- Docker 설정 변경
- 포트 할당

### 5단계: Phase 분할

작업을 롤백 안전 단위로 분할한다:
- 각 Phase 완료 후 시스템 정상 동작 가능
- 의존성 없는 작업은 병렬 표기
- Phase별 검증 항목 포함

### 6단계: 파일 생성

`docs/Plans/[계획명]/` 폴더에 다음 파일을 생성한다:
- `orchestration.md` — 중심 문서 (문제 정의, AS-IS/TO-BE, Phase 체크리스트)
- `phase-N-[제목].md` — Phase별 상세 실행 계획
- `execution-log.md` — 실행 기록 템플릿

### 7단계: 자동 검토

생성한 계획에 대해 `/review-plan`을 호출한다.
- 최대 3회 리뷰-수정 루프
- `/review-plan`이 "재계획 필요" 판정 시 `/plan-edit`으로 수정 후 재검토

### 8단계: Plan 모드 제출

검토 통과 후 EnterPlanMode → ExitPlanMode로 최종 계획을 제출한다.
