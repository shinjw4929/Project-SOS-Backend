---
name: review-plan
description: 구현 계획을 프로젝트 컨벤션과 코드베이스 기준으로 검토합니다. 문제가 있으면 사용자 확인 후 /plan-edit로 수정하고 재검토합니다 (최대 1회).
allowed-tools: Read, Grep, Glob, Bash, Edit, Write, Agent, EnterPlanMode, ExitPlanMode, Skill, AskUserQuestion
---

## 역할

구현 계획을 프로젝트 컨벤션과 코드베이스 기준으로 검토한다. 문제가 있으면 사용자 확인 후 `/plan-edit`를 호출하여 수정하고 재검토한다.

## Plan 검토 절차

### 0단계: 검토 대상 확인

$ARGUMENTS를 다음 순서로 해석한다:

1. **경로 매칭**: $ARGUMENTS가 파일 경로이면 해당 파일을 읽는다.
2. **기능명 매칭**: `docs/Plans/[$ARGUMENTS]/orchestration.md`가 존재하면 해당 계획을 로드한다.
3. **현재 대화 매칭**: 위에 해당하지 않으면, 현재 대화에서 가장 최근에 생성한 구현 계획을 대상으로 삼는다.
4. **탐색**: 어디에도 해당하지 않으면 `docs/Plans/*/orchestration.md`를 Glob으로 탐색하여 `AskUserQuestion` 도구로 선택지를 제시한다 (방향키 선택 가능).

$ARGUMENTS에서 경로/기능명 외 추가 텍스트가 있으면 추가 검토 관점으로 반영한다.

확인된 계획의 orchestration.md + 모든 phase 파일을 읽어 전문을 보관한다 (에이전트 프롬프트 전달용).

### 1단계: 워크트리 격리 검토 에이전트 실행

Agent를 호출하여 검토를 수행한다. 반드시 다음 파라미터를 설정:
- `isolation: "worktree"` (필수)
- `subagent_type: "general-purpose"`
- `description: "Plan 검토"`

에이전트 프롬프트 구조:
```
당신은 C++ 백엔드 프로젝트의 구현 계획 검토 전문가입니다.

## 검토 대상 계획
[0단계에서 수집한 orchestration + phase 전문]

## 지시사항
1. 다음 문서를 읽어 프로젝트 컨벤션을 파악한다:
   - CLAUDE.md (Architecture, Redis 키 구조, Port Assignment, Development Guidelines)
   - 계획 내용과 관련된 docs/Systems/ 모듈별 문서
2. 계획에서 언급된 기존 클래스/설정/Redis 키가 실제로 존재하는지 Grep/Glob으로 확인한다.
3. docs/Checklists/review-code-checklist.md를 읽고 다음 관점에서 검증한다:
   - 아키텍처 적합성: 모듈 분리(common/room/chat/infra), 서비스 간 통신 방식, 기존 패턴 일관성
   - 중복/충돌 검사: 기존 코드/설정 재구현, 인프라 설정 충돌(포트, Docker), common 라이브러리 미활용
   - C++ 설계 원칙: RAII, smart pointer, 스레드 안전성(strand), 에러 처리 전략
   - 네트워크/인프라: Protobuf 메시지 정의, Rate Limiting/인증/연결 관리, Docker 구성 호환성
   - 누락 사항: 문서 업데이트 계획, 테스트 계획, 엣지 케이스(연결 끊김, 타임아웃, Redis 장애)
4. 사실 기반으로 검토. 코드베이스에서 근거를 확인한 항목만 지적. 추측/과잉 지적 금지.

## 반환 형식 (이 형식만 반환)
## Plan 검토 결과
### 적합 항목
- (문제 없는 부분 간략히)
### 수정 필요
| # | 항목 | 현재 계획 | 수정 제안 | 이유 |
### 누락 사항
- (계획에서 빠진 부분)
### 최종 판단
(승인 가능 / 재계획 필요)
```

### 2단계: 결과 출력 및 후속 행동

에이전트 결과를 그대로 출력한다.

- **"승인 가능"**: 계획 구조가 올바른 경우. 수정 항목이 있으면 플랜 파일에 직접 반영(Edit)한 뒤 종료한다.
- **"재계획 필요"**: 모듈 배치/의존성/접근 방식 등 계획 구조 자체에 문제가 있는 경우:
  1. 수정이 필요한 항목을 요약하고, `/plan-edit`로 수정을 진행할지 사용자에게 확인을 요청한다.
  2. 사용자가 승인하면 `/plan-edit` 스킬을 호출하여 계획을 수정한다. 이때 검토에서 발견된 수정 항목을 $ARGUMENTS로 전달한다.
  3. `/plan-edit` 완료 후, 수정된 계획에 대해 0단계부터 재검토를 수행한다.

### 재검토 루프 제한

`review-plan → plan-edit → review-plan` 루프는 **최대 1회**까지 반복한다.

1회 재검토 후에도 "재계획 필요"이면 루프를 중단하고, 남은 문제를 사용자에게 보고하여 수동 판단을 요청한다.
