---
name: plan-edit
description: 기존 구현 계획을 부분 수정합니다. 완료된 Phase는 보호합니다.
allowed-tools: Read, Edit, Write, Grep, Glob, Bash, Agent
---

## 계획 수정 실행

### 1단계: 대상 계획 로드

$ARGUMENTS에서 수정 대상 계획을 찾는다:
- 경로/이름으로 지정
- 또는 `docs/Plans/`에서 Glob 탐색

다음 파일을 읽는다:
- `orchestration.md` — 전체 구조 파악
- `execution-log.md` — 완료/미실행 Phase 상태 파악
- 관련 Phase 파일

### 2단계: 수정 요구사항 확인

수정 유형을 파악한다:
- Phase 내용 수정
- 새 Phase 추가
- Phase 삭제
- Phase 순서 변경
- AS-IS/TO-BE 변경

### 3단계: 영향 분석

- **완료된 Phase와의 충돌**: 완료 Phase에 의존하는 수정은 경고
- **의존성 체인**: 수정이 후속 Phase에 미치는 영향
- **코드베이스 검증**: 수정 내용이 실제 코드/설정과 일치하는지 확인

### 4단계: 수정 실행

**보호 규칙**:
- 완료된 Phase는 **절대 수정하지 않는다**
- Phase 번호가 변경되면 모든 참조 (orchestration.md, execution-log.md)를 동기 업데이트

**수정 파일**:
- Phase 파일 (내용 수정/추가/삭제)
- `orchestration.md` (Phase 체크리스트, 의존성 업데이트)
- `execution-log.md` (수정 이력 기록)

### 5단계: 정합성 검증

자동으로 다음을 확인한다:
- Phase 번호 연속성
- 파일 참조 유효성 (orchestration ↔ Phase 파일)
- 의존성 무결성 (순환 의존 없음)

### 6단계: 변경 보고

수정 내용을 사용자에게 보고한다:
- 변경된 Phase 목록
- Phase 구조 테이블 (before/after)
- 주의 사항
