---
name: plan-execute
description: 구현 계획을 Phase별로 순차 실행하고 execution-log를 업데이트합니다.
allowed-tools: Read, Edit, Write, Grep, Glob, Bash, Agent, Skill, EnterPlanMode, ExitPlanMode
---

## 계획 실행

### 1단계: 계획 로드

실행 대상 계획을 찾는다:
- $ARGUMENTS에 경로/이름이 있으면 해당 계획
- 없으면 `docs/Plans/` 에서 가장 최근 계획 또는 현재 대화의 계획
- `orchestration.md`와 `execution-log.md`를 읽어 미실행 Phase를 파악

### 2단계: 다음 Phase 선택

execution-log.md에서 "대기" 상태인 Phase 중 전제 조건이 충족된 것을 선택한다.

### 3단계: Phase 실행

Phase 파일의 작업 목록을 순서대로 실행한다:

1. `docs/Systems/` 관련 문서를 읽어 현재 상태 파악
2. `docs/Checklists/pattern-search-guide.md`로 참조 패턴 탐색
3. 병렬 작업: 독립적인 작업은 Agent를 활용하여 동시 진행
4. 순차 작업: 의존성 순서대로 실행

### 4단계: Phase 검증

Phase 파일의 검증 항목을 확인한다:
- 빌드 성공 여부
- 테스트 통과 여부
- Phase 특화 검증 항목

### 5단계: 기록 업데이트

- `orchestration.md`의 Phase 체크리스트 상태 업데이트
- `execution-log.md`에 시작/완료 시각, 결과, 비고 기록
- 2~5단계를 미실행 Phase가 없을 때까지 반복

### 6단계: 후처리 (자동)

전체 Phase 완료 후:
1. `/review-comments` — 변경 파일의 주석 정합성 점검
2. `/update-docs` — 관련 docs 문서 업데이트

### 7단계: 계획 아카이브 (필수)

계획 폴더를 `docs/Plans/Completed/`로 이동한다. 이 단계는 생략할 수 없다.

### 8단계: 최종 보고

사용자에게 다음을 보고한다:
- 실행한 Phase 수
- 변경/생성한 파일 목록
- 검증 결과
- `/review-code`, `/commit` 권고
