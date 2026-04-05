---
name: plan-execute
description: docs/Plans/ 아래의 구현 계획(오케스트레이션)을 읽고 Phase별로 순차 실행하며, 실행 기록을 자동 갱신합니다.
allowed-tools: Read, Edit, Write, Grep, Glob, Bash, Agent, Skill, EnterPlanMode, ExitPlanMode, AskUserQuestion
---

## 역할

기존에 생성된 구현 계획(오케스트레이션 파일)을 읽고, Phase를 순차적으로 실행한다.
각 Phase 완료 시 `execution-log.md`를 자동 갱신한다.

$ARGUMENTS로 오케스트레이션 파일 경로 또는 기능명을 전달받는다.

## 실행 절차

### 0단계: 오케스트레이션 로드

1. $ARGUMENTS에서 경로를 추출한다.
   - 경로가 주어지면 해당 파일을 읽는다.
   - 기능명만 주어지면 `docs/Plans/[기능명]/orchestration.md`를 찾는다.
   - 없으면 `docs/Plans/*/orchestration.md`를 Glob으로 탐색하여 `AskUserQuestion` 도구로 선택지를 제시한다 (방향키 선택 가능).
2. 오케스트레이션 파일에서 Phase 체크리스트와 의존성을 파악한다.
3. `execution-log.md`를 읽어 이미 완료된 Phase를 확인한다. 완료된 Phase는 건너뛴다.

### 1단계: 다음 Phase 결정

1. 완료되지 않은 Phase 목록을 추출한다.
2. `AskUserQuestion` 도구로 실행할 Phase를 선택지로 제시한다 (방향키로 선택 가능).
   - 선행 조건이 충족된 가장 빠른 Phase를 첫 번째 옵션으로 놓고 "(Recommended)"를 붙인다.
   - 나머지 미완료 Phase도 옵션에 포함한다 (최대 4개, 초과 시 "Other"로 직접 입력 가능).
   - "전체 자동 실행" 옵션도 포함한다.
   - 각 옵션의 description에 Phase 요약을 간략히 기재한다.
3. 사용자가 선택한 Phase 파일을 읽어 작업 목록, 테스트 요구사항, 완료 기준을 확인한 뒤 바로 실행에 들어간다.

### 2단계: Phase 실행

Phase 파일의 작업 목록을 순서대로 수행한다:

1. **Docs 참조**: 작업 대상 모듈의 관련 문서를 먼저 읽는다.
2. **패턴 탐색**: 코드를 작성/수정하기 전에 `docs/Checklists/pattern-search-guide.md`를 따라 기존 레퍼런스를 탐색하고 패턴을 추출한다.
3. **병렬 작업**: Phase 파일에 subagent 병렬 구성이 명시된 경우, Agent 도구로 독립적인 작업을 병렬 실행한다. 같은 파일을 수정하는 작업은 병렬로 실행하지 않는다. 각 Agent에게 레퍼런스 코드와 패턴 가이드 경로를 함께 전달한다.
4. **순차 작업**: 의존성이 있는 작업은 순서대로 실행한다.
5. **CLAUDE.md 준수**: 구현 시 CLAUDE.md의 Development Guidelines를 따른다.

### 3단계: Phase 검증

Phase 파일의 검증 방법과 완료 기준을 확인한다:

1. 컴파일 확인: 소스 파일이 변경된 경우 `/build`로 빌드 검증을 권장한다.
2. 테스트 실행: Phase 파일에 명시된 테스트가 있으면 `/test`로 실행한다.
3. 완료 기준 체크리스트 점검

검증 실패 시:
- 실패 원인을 분석하고 수정한다.
- 수정 후 재검증한다.
- 반복 실패 시 사용자에게 보고하고, 다음 선택지를 안내한다:
  - 직접 수정 후 재검증 시도
  - `/plan-edit [기능명]`으로 남은 Phase를 재구성
  - 현재 Phase를 Fail로 기록하고 중단

### 4단계: 기록 갱신

Phase 완료 후 다음을 수행한다:

**A. orchestration.md 체크박스 갱신:**
- 완료된 작업 항목을 `[x]`로 체크한다.

**B. execution-log.md 갱신:**

```markdown
## Phase N: [제목] - [날짜]

### 실행 내역
| 작업 | 결과 | 비고 |
|---|---|---|
| ... | ... | ... |

### 변경된 파일
- `path/to/file.cpp` - 변경 내용 요약

### 발견된 이슈
- [이슈] → [대응]

### Phase N 완료 판정: Pass / Fail
```

**C. Phase 파일 체크박스 갱신:**
- 완료 기준 체크박스를 모두 체크한다.

### 5단계: 후속 처리

1. 다음 Phase가 남아있으면 1단계로 돌아간다.
2. 모든 Phase가 완료되면 다음을 **순서대로 모두** 수행한다:
   1. `/sync-comments`를 자동 호출하여 주석 정합성을 점검한다.
   2. `/sync-docs`를 자동 호출하여 docs 문서를 동기화한다.
   3. **(필수)** 계획 폴더를 `docs/Plans/Completed/`로 이동한다. 이 단계를 건너뛰지 않는다:
      ```
      mkdir -p "docs/Plans/Completed" && mv "docs/Plans/[기능명]" "docs/Plans/Completed/"
      ```
   4. 사용자에게 최종 보고 (변경 파일 수, 주요 변경 사항 요약)
   5. `/review-code`로 최종 코드 리뷰를 권장
   6. `/commit`으로 커밋을 권장

## 주의사항

- **Phase 순서 엄수**: 선행 조건이 충족되지 않은 Phase는 실행하지 않는다.
- **검증 실패 시 중단**: 검증을 통과하지 못한 Phase가 있으면 다음 Phase로 넘어가지 않는다.
- **사용자 확인**: 각 Phase 시작 전 사용자 확인을 받는다. 사용자가 명시적으로 "전체 실행" 또는 "자동 진행"을 요청한 경우에만 확인 없이 연속 실행한다.
- **실행 기록 즉시 갱신**: Phase 완료 즉시 execution-log.md를 갱신한다. 여러 Phase를 묶어서 나중에 기록하지 않는다.
- **기존 코드 존중**: Phase 파일에 명시되지 않은 리팩토링이나 개선을 임의로 수행하지 않는다.
- **WorkLog 미사용**: 계획 실행 기록은 execution-log.md에 기록한다. WorkLog는 계획 밖 단발 작업 전용이다.
- **완료된 계획 이동 필수**: 모든 Phase 완료 후 계획 폴더를 반드시 `docs/Plans/Completed/`로 이동한다. 이 단계를 누락하면 다음 `/plan-execute` 호출 시 완료된 계획이 재탐색된다.
