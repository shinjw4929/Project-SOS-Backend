---
name: plan-edit
description: 기존 구현 계획(docs/Plans/)을 부분 수정합니다. 완료된 Phase는 보존하고 미실행 Phase만 변경하며, orchestration/phase 파일 간 정합성을 자동 유지합니다.
allowed-tools: Read, Edit, Write, Grep, Glob, Bash, Agent, AskUserQuestion
---

## 역할

이미 생성된 구현 계획을 부분 수정한다. 완료된 Phase의 실행 이력을 보존하면서, 미실행 Phase를 추가/삭제/수정한다.

$ARGUMENTS로 기능명 또는 오케스트레이션 파일 경로를 전달받는다.

## 수정 절차

### 0단계: 계획 로드 및 현재 상태 파악

1. $ARGUMENTS에서 대상 계획을 특정한다.
   - 경로가 주어지면 해당 파일을 읽는다.
   - 기능명만 주어지면 `docs/Plans/[기능명]/orchestration.md`를 찾는다.
   - 없으면 `docs/Plans/*/orchestration.md`를 Glob으로 탐색하여 `AskUserQuestion` 도구로 선택지를 제시한다 (방향키 선택 가능).
2. orchestration.md를 읽어 전체 Phase 구조를 파악한다.
3. execution-log.md를 읽어 완료된 Phase를 식별한다.
4. 모든 phase-N-*.md 파일을 읽어 각 Phase의 상세 내용을 파악한다.

결과를 요약하여 사용자에게 보여준다:
```
## 현재 계획 상태
- 계획: [기능명]
- 전체 Phase: N개
- 완료: Phase 1, 2 (execution-log 기준)
- 미실행: Phase 3, 4, 5
- 수정 가능 범위: Phase 3~5
```

### 1단계: 수정 요구사항 확인

사용자로부터 수정 내용을 확인한다. $ARGUMENTS에 구체적 지시가 있으면 추출한다. 없으면 질문한다.

수정 유형:

| 유형 | 설명 |
|---|---|
| **Phase 내용 수정** | 기존 Phase의 태스크, 완료 기준, 테스트 요구사항 변경 |
| **Phase 추가** | 새 Phase 삽입 (기존 Phase 번호 재조정) |
| **Phase 삭제** | 미실행 Phase 제거 |
| **Phase 순서 변경** | Phase 간 의존성 재구성 |
| **AS-IS/TO-BE 수정** | 요구사항 변경에 따른 설계 수정 |

### 2단계: 영향 범위 분석

수정 내용이 기존 계획에 미치는 영향을 분석한다:

1. **Phase 간 의존성**: 수정되는 Phase를 선행 조건으로 참조하는 다른 Phase가 있는지 확인
2. **완료된 Phase와의 충돌**: 수정 내용이 이미 완료된 Phase의 결과물과 모순되지 않는지 확인
3. **변경 파일 요약 갱신**: orchestration.md의 변경 파일 요약이 수정 후에도 정확한지 확인

충돌이 발견되면 사용자에게 보고하고 판단을 요청한다.

### 3단계: 코드베이스 검증 (필요 시)

수정 내용이 코드 변경을 수반하는 경우:
- 수정된 Phase에서 참조하는 파일/클래스/함수가 실제로 존재하는지 Grep/Glob으로 확인
- 완료된 Phase에서 이미 변경된 코드 상태를 반영하여 AS-IS를 갱신

### 4단계: 계획 파일 수정

수정 사항을 사용자에게 요약한 뒤 확인을 받고, 다음 파일을 갱신한다:

**A. Phase 파일 수정/생성/삭제**
- 내용 수정: 해당 phase-N-*.md를 Edit
- Phase 추가: 새 phase-N-*.md 파일을 Write하고, 기존 Phase 파일의 번호가 밀리면 파일명을 변경
- Phase 삭제: 해당 phase-N-*.md 파일을 삭제하고, 후속 Phase 번호 재조정

**B. orchestration.md 갱신**
- Phase 체크리스트 갱신 (추가/삭제/순서 반영)
- Phase 간 의존성 테이블 갱신
- 변경 파일 요약 갱신
- AS-IS/TO-BE 수정 (해당 시)

**C. execution-log.md에 수정 이력 기록**

```markdown
## 계획 수정 - [날짜]

### 수정 사유
- (왜 수정했는지)

### 수정 내역
| 항목 | 변경 전 | 변경 후 |
|---|---|---|
| Phase 3 | [기존 내용 요약] | [수정 내용 요약] |
| Phase 6 | (없음) | 신규 추가 |

### 영향받는 Phase
- Phase 3: 태스크 2개 추가
- Phase 4: 선행 조건 변경 없음
```

### 5단계: 정합성 검증

수정 완료 후 다음을 자동 검증한다:

1. **Phase 번호 연속성**: 1부터 빠짐 없이 연속인지 확인
2. **파일 참조 일치**: orchestration.md의 Phase 링크가 실제 파일과 일치하는지 확인
3. **의존성 무결성**: 선행 조건에 명시된 Phase가 모두 존재하는지 확인
4. **완료 Phase 보존**: 완료된 Phase의 체크박스(`[x]`)와 execution-log 내용이 변경되지 않았는지 확인

검증 실패 시 자동으로 수정한다.

### 6단계: 결과 보고

```
## 계획 수정 완료

### 수정된 파일
- orchestration.md: [변경 요약]
- phase-3-xxx.md: [변경 요약]
- phase-6-xxx.md: 신규 생성
- execution-log.md: 수정 이력 추가

### 수정 후 Phase 구조
| Phase | 제목 | 상태 | 변경 여부 |
|-------|------|------|-----------|
| 1 | ... | 완료 | - |
| 2 | ... | 완료 | - |
| 3 | ... | 미실행 | 수정됨 |
| 4 | ... | 미실행 | 변경 없음 |
| 5 | ... | 미실행 | 신규 |

### 다음 단계
- `/plan-execute [기능명]`으로 수정된 계획을 이어서 실행
- `/review-plan`으로 수정된 계획을 재검토
```

## 주의사항

- **완료된 Phase 보호**: 완료 판정이 내려진 Phase의 내용과 execution-log 기록은 절대 수정하지 않는다. 완료된 Phase를 수정해야 하는 경우, 사용자에게 명시적으로 경고하고 확인을 받는다.
- **번호 재조정 주의**: Phase 추가/삭제 시 파일명, orchestration.md 링크, 의존성 테이블, execution-log 참조를 모두 일관되게 갱신한다.
- **계획 작성 가이드 준수**: `docs/Plans/계획-작성-가이드.md`의 포맷과 규칙을 따른다.
- **CLAUDE.md 준수**: 수정된 Phase의 구현 내용이 Development Guidelines를 따르는지 확인한다.
- **최소 변경 원칙**: 요청된 수정에만 집중한다. 수정 대상이 아닌 Phase를 임의로 개선하지 않는다.
