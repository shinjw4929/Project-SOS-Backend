---
name: port-skill
description: Unity 프로젝트(Project-SOS)의 스킬을 읽어 C++ 백엔드(Project-SOS-Backend) 컨텍스트에 맞게 변환하여 작성합니다.
allowed-tools: Read, Write, Edit, Grep, Glob, Bash, AskUserQuestion
---

## 역할

Unity 프로젝트의 스킬 SKILL.md를 읽고, C++ 백엔드 프로젝트의 기술 스택/컨벤션/모듈 구조에 맞게 변환하여 백엔드 `.claude/skills/`에 작성한다.

$ARGUMENTS가 있으면 포팅할 Unity 스킬명으로 반영한다. 없으면 사용자에게 질문한다.

## 실행 절차

### 1단계: 포팅 대상 확인

$ARGUMENTS에서 Unity 스킬명을 추출한다. 없으면 Unity 스킬 목록을 Glob으로 탐색하여 `AskUserQuestion`으로 선택지를 제시한다.

```
소스: C:\Users\sjw49\Unity Projects\Project-SOS\.claude\skills\{스킬명}\SKILL.md
대상: C:\Users\sjw49\Unity Projects\Project-SOS-Backend\.claude\skills\{스킬명}\SKILL.md
```

백엔드에 동일 이름의 스킬이 이미 존재하면 **덮어쓸지** 사용자에게 확인한다.

### 2단계: 소스 스킬 + 컨텍스트 수집

다음을 병렬로 읽는다:
1. **Unity 스킬 원본**: 소스 SKILL.md 전체
2. **백엔드 기존 스킬 1개**: 동일 Tier의 백엔드 스킬을 읽어 출력 포맷/구조 레퍼런스로 사용
3. **백엔드 SKILLS.md**: 현재 스킬 계층 구조 파악

### 3단계: 용어/개념 변환

아래 매핑 테이블을 적용하여 Unity 스킬의 내용을 C++ 백엔드에 맞게 변환한다.

**기술 스택 매핑**:

| Unity (소스) | Backend (대상) |
|---|---|
| `.cs` 파일 | `.cpp`, `.h`, `.hpp` 파일 |
| Unity Editor, 배치 모드 | Developer Command Prompt, CMake |
| EditMode/PlayMode 테스트 | CTest + Catch2 |
| Unity CLI 빌드 | `cmake --build build` |
| Burst/Job System | 스레드 안전성(strand), RAII |
| `[BurstCompile]`, `[MethodImpl]` | (해당 없음 - 제거) |
| `SystemAPI`, `EntityManager` | Boost.Asio, Protobuf API |
| `SubScene`, `Authoring`, `Baker` | (해당 없음 - 제거) |
| `GameSettings` 싱글톤 | `Config` 클래스 |

**아키텍처 매핑**:

| Unity (소스) | Backend (대상) |
|---|---|
| 어셈블리 (Client/Server/Shared) | 모듈 (common/room/chat/infra) |
| SystemGroup, UpdateAfter | (해당 없음 - 제거) |
| Ghost 동기화, GhostField | (해당 없음 - 제거) |
| RPC (Client↔Server) | Protobuf Envelope, 내부 채널 |
| ECS 컴포넌트/시스템 | 클래스/모듈 |
| DamageEvent 버퍼 | (해당 없음 - 제거) |
| Collider, Physics | (해당 없음 - 제거) |

**문서/경로 매핑**:

| Unity (소스) | Backend (대상) |
|---|---|
| `Docs/` | `docs/` |
| `Docs/Architecture.md` | `docs/Systems/` 모듈별 문서 |
| `Docs/GameDesign.md` | (해당 없음 - 제거) |
| `Docs/Systems/시스템 그룹 및 의존성.md` | `docs/Systems/` 모듈별 문서 |
| `Docs/Checklists/` | `docs/Checklists/` |
| `Docs/Plans/` | `docs/Plans/` |

**리뷰 심각도 매핑**:

| Unity 치명 항목 | Backend 치명 항목 |
|---|---|
| 데이터 레이스, Health 직접 수정 | 메모리 누수, data race, use-after-free |
| Ghost 비동기, Persistent Dispose 누락 | 보안 취약점, 서비스 크래시 |
| Burst 위반, RefRO 쓰기 | RAII 미적용, shared_ptr 순환 참조 |

### 4단계: 스킬 파일 작성

변환된 내용으로 백엔드 `.claude/skills/{스킬명}/SKILL.md`를 작성한다.

**작성 규칙**:
1. **200줄 이내** 유지
2. **프론트매터**: description을 백엔드 맥락으로 재작성. allowed-tools는 원본 유지하되 불필요한 도구 제거
3. **Unity 전용 개념 제거**: 변환 불가능한 Unity/DOTS 전용 내용(Burst, Ghost, Baker 등)은 삭제
4. **백엔드 전용 개념 추가**: Redis, Docker, Protobuf, 내부 채널 등 백엔드 고유 항목 추가
5. **기존 백엔드 스킬과 포맷 일관성**: 2단계에서 읽은 레퍼런스의 마크다운 구조를 따름

### 5단계: SKILLS.md 업데이트

백엔드 `.claude/skills/SKILLS.md`에 새 스킬을 등록한다:
- 적절한 Tier에 배치
- 시나리오 섹션에 사용 예시 추가 (필요 시)

### 6단계: 결과 보고

```
## 스킬 포팅 완료

### 소스 → 대상
| 항목 | 값 |
|------|---|
| Unity 원본 | .claude/skills/{name}/SKILL.md |
| Backend 결과 | .claude/skills/{name}/SKILL.md |
| 줄 수 | N줄 (원본 M줄) |

### 변환 요약
| 변환 유형 | 건수 |
|-----------|------|
| 용어 치환 | N건 |
| 섹션 제거 (Unity 전용) | N건 |
| 섹션 추가 (Backend 전용) | N건 |

### Tier 배치
{Tier N}: {역할 설명}
```

## 주의사항

- **맹목적 치환 금지**: 매핑 테이블을 기계적으로 적용하지 않는다. 문맥에 맞지 않는 치환은 건너뛴다.
- **백엔드 스킬 레퍼런스 필수**: 동일 Tier의 기존 백엔드 스킬을 최소 1개 읽고, 그 구조/톤을 따른다.
- **Unity 전용 삭제 시 공백 방지**: Unity 전용 섹션을 제거한 뒤 논리적으로 비어버리는 단계가 있으면, 해당 단계를 백엔드 고유 내용으로 채우거나 번호를 재조정한다.
- **200줄 제한**: 원본이 200줄 이하더라도 변환 후 200줄을 초과하면 압축한다.
- **덮어쓰기 확인**: 백엔드에 동일 이름 스킬이 있으면 반드시 사용자 확인 후 덮어쓴다.
