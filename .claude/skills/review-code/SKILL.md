---
name: review-code
description: 코드 변경사항을 3개 도메인 에이전트(안전성/네트워크/컨벤션)가 병렬로 리뷰합니다. 사용자가 코드 리뷰, 변경점 검토, 또는 /review-code를 요청할 때 실행합니다.
allowed-tools: Read, Grep, Glob, Bash, Agent, Edit, AskUserQuestion
---

## 코드 리뷰 실행

$ARGUMENTS가 있으면 해당 내용을 리뷰 범위/관점으로 반영한다. 유효한 인자 형식: 파일 경로(`src/room/...`), 커밋 범위(`HEAD~3..HEAD`), 관점 키워드(`성능 중심`, `네트워크 집중` 등). 인자가 없으면 현재 워킹 트리의 전체 소스 변경을 대상으로 한다.

### 1단계: 변경사항 수집

**인자에 따른 분기:**
- **인자 없음 / 관점 키워드만**: `git diff HEAD --name-only`, `git diff HEAD`, `git status` 병렬 실행
- **파일 경로**: 해당 파일만 `git diff HEAD -- <파일>` + `git status`
- **커밋 범위** (`A..B`, `HEAD~N..HEAD` 등): `git diff <범위> --name-only`, `git diff <범위>` 사용 (`git diff HEAD` 대체)

공통: `git status`는 `-uall` 플래그 사용 금지.

변경된 소스 파일(`.cpp`, `.h`, `.hpp`, `.proto`, `.toml`, `.yml`, `.yaml`, `.sql`)만 리뷰 대상으로 삼는다. untracked 소스 파일도 포함하여 전체 내용을 읽는다. 바이너리, 빌드 산출물은 제외. **변경된 소스 파일이 없으면 "리뷰 대상 없음"을 출력하고 종료한다.**

### 2단계: 체크리스트 및 설계 문서 로드

다음을 병렬 Read (에이전트 프롬프트에 직접 포함):

| 문서 | 전달 대상 에이전트 |
|------|---------------------|
| `docs/Checklists/review-code-checklist.md` 섹션 A, E2 | 안전성 |
| `docs/Checklists/review-code-checklist.md` 섹션 B, C, E3 | 네트워크 |
| `docs/Checklists/review-code-checklist.md` 섹션 D, E1 | 컨벤션 |
| 변경 모듈의 `docs/Systems/` 문서 | 컨벤션 |

변경된 모듈에 대응하는 `docs/Systems/` 문서는 `docs/Documentation-Checklist.md` 매핑을 참조하여 결정한다.

### 3단계: 3개 도메인 에이전트 병렬 실행

**반드시 3개 Agent를 단일 메시지에서 동시 호출.** 각 Agent: `isolation: "worktree"`, `subagent_type: "general-purpose"`.

각 프롬프트에 변경 파일 목록 + diff 전문 + 해당 체크리스트 전문을 포함. 공통 지시:
- 변경/신규 코드만 리뷰. 기존 코드 문제 지적 금지.
- 각 파일과 주변 코드를 Read하여 맥락 파악.
- 체크리스트 항목을 diff에 대입하여 위반 판정. 추측 금지.
- 반환 형식 (없으면 `없음`):
  ```
  | # | 도메인 | 심각도 | 파일:라인 | 항목 | 설명 | 제안 |
  ```

**에이전트별 차이:**
- **[안전성]**: review-code-checklist.md 섹션 A (A1~A10), E2. RAII 미적용, smart pointer 오용, 리소스 누수, use-after-free, dangling reference, 불필요한 복사, O(n^2) 루프에 주목.
- **[네트워크]**: review-code-checklist.md 섹션 B (B1~B4), C (C1~C4), E3. Protobuf Envelope dispatch, Rate Limiting, 인증/검증, 연결 관리, Docker/ClickHouse/Vector 설정 정합성, 엣지 케이스(동시 접속, 중복 요청, Redis 장애)에 주목.
- **[컨벤션]**: review-code-checklist.md 섹션 D (D1~D6), E1 + 해당 모듈의 `docs/Systems/` 문서. 네이밍, Config 패턴, 기존 유틸리티 중복, 로그 형식, 폴더 구조에 주목. 설계 문서와의 모듈 경계/책임 정합성도 검증.

### 4단계: 결과 병합 및 출력

`없음`인 도메인 제외. 동일 파일:라인 중복은 높은 심각도 유지. 심각도 정렬: 치명 > 경고 > 제안.

```
## 코드 리뷰 결과

### 리뷰 대상
- 변경 파일: N개 (+X / -Y 라인), 에이전트: 안전성 / 네트워크 / 컨벤션

### 문제 발견
| # | 도메인 | 심각도 | 파일:라인 | 항목 | 설명 | 제안 |

### 잘된 점
- (컨벤션을 잘 따른 부분, 좋은 설계 판단)

### 최종 판단
(승인 가능 / 수정 필요)
```

**심각도 기준**:
- **치명**: 메모리 누수(RAII 미적용, shared_ptr 순환), data race(strand 미사용), 보안 취약점(인증 우회, 버퍼 오버플로), 서비스 크래시(use-after-free)
- **경고**: 컨벤션 위반, 에러 처리 미흡, 성능 문제(불필요한 복사), Protobuf 불일치, Rate Limiting 누락, 인프라 설정 불일치
- **제안**: 더 나은 대안 존재, 가독성 개선, 사소한 네이밍, const correctness

### 5단계: 후속 행동

- **승인 가능**: 종료.
- **수정 필요**: 사용자에게 자동 수정 여부를 확인한다. 승인 시 치명/경고 항목을 코드에 반영한다. 패턴 불일치가 주요 원인이면 `/implement`로 재구현을 권장.

### 주의사항

- **변경/신규 코드만 리뷰**: 기존 코드 문제 지적 금지 (변경으로 인한 정합성 파괴는 예외). untracked 소스는 전체 내용 리뷰.
- **사실 기반**: 코드베이스에서 근거 확인된 항목만 지적. 추측/과잉 지적 금지.
- **효율 우선**: 사소한 스타일보다 런타임 영향 큰 문제에 집중.
- **에이전트 프롬프트에 데이터 직접 포함**: 체크리스트/diff를 프롬프트에 포함하여 추가 Read 최소화.
