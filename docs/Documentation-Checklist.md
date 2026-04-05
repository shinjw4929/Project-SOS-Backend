# Documentation Checklist

## Docs 폴더 구조

```
docs/
├── 시스템 아키텍처.md              # 전체 백엔드 시스템 구조
├── 로깅 파이프라인.md              # Vector → ClickHouse → Grafana 상세
├── Documentation-Checklist.md     # 본 문서: 문서 관리 규칙
│
├── Checklists/                    # 스킬 공유 참조 문서
│   ├── pattern-search-guide.md    # C++ 패턴 탐색 가이드
│   └── review-code-checklist.md   # C++ 코드 리뷰 체크리스트
│
├── Systems/                       # 모듈별 상세 문서
│   ├── common 라이브러리.md        # Codec, Logger, Config, Redis, RateLimiter
│   ├── Room Server.md             # 방 관리, TCP, 내부 채널
│   ├── Chat Server.md             # 채팅, 인증, 세션 채널
│   ├── 인프라 구성.md              # Docker, Redis, ClickHouse, Vector, Grafana
│   └── 테스트.md                  # Catch2 유닛, Python E2E/부하, Vector 파싱
│
├── Plans/                         # 구현 계획
│   ├── 계획-작성-가이드.md          # 계획 템플릿
│   └── Completed/                 # 완료된 계획 아카이브
│       ├── 구현 우선순위.md         # Phase별 구현 순서
│       ├── 로깅/                   # 로깅 시스템 설계 문서
│       ├── 로깅 통합 운영/          # 로깅 통합 운영 계획
│       ├── 룸 서버/                # Room Server 설계 문서
│       ├── 채팅 서버/              # Chat Server 설계 문서
│       ├── 하네스 엔지니어링/       # 하네스 엔지니어링 체크리스트
│       ├── Room-List-Push-Broadcast/  # 방 목록 Push 브로드캐스트
│       └── 게임서버-중복세션-방지/      # 게임서버 중복세션 방지
│
├── Internal/
│   ├── Analysis/                  # 분석 문서
│   ├── ClickHouse/
│   │   └── 사용법.md              # ClickHouse 접속, 쿼리, 데이터 관리
│   └── Grafana/
│       └── 사용법.md              # Grafana 대시보드, 알림, 운영
│
└── WorkLog/                       # 날짜별 구현 기록
    └── YYYY-MM-DD/
```

---

## 변경 유형별 업데이트 대상

| 변경 유형 | 업데이트 대상 문서 |
|----------|-------------------|
| common 라이브러리 변경 | `docs/Systems/common 라이브러리.md` |
| Room Server 코드 변경 | `docs/Systems/Room Server.md` |
| Chat Server 코드 변경 | `docs/Systems/Chat Server.md` |
| Protobuf 변경 | 관련 서버 Systems 문서 |
| Docker Compose 변경 | `docs/Systems/인프라 구성.md` |
| ClickHouse 스키마 변경 | `docs/Systems/인프라 구성.md`, `docs/로깅 파이프라인.md` |
| Vector 설정 변경 | `docs/Systems/인프라 구성.md`, `docs/로깅 파이프라인.md` |
| Grafana 대시보드/알림 변경 | `docs/Systems/인프라 구성.md` |
| Redis 키 구조 변경 | 관련 서버 Systems 문서, `CLAUDE.md` Redis 키 테이블 |
| 포트 할당 변경 | `docs/시스템 아키텍처.md`, `CLAUDE.md` |
| 새 인프라 컴포넌트 추가 | `docs/시스템 아키텍처.md`, `docs/Systems/인프라 구성.md` |
| 새 서버/모듈 추가 | `docs/시스템 아키텍처.md`, 새 Systems 문서 작성 |
| 테스트 추가/변경 | `docs/Systems/테스트.md` |

---

## 문서 작성 원칙

1. **코드와 동기화**: 문서 내용이 실제 코드/설정과 일치해야 함
2. **간결함 유지**: 핵심 구조와 설정값 중심으로 작성. 코드를 다시 작성하지 않는다
3. **예제 포함**: 복잡한 설정은 코드/명령어 예제로 설명
4. **형식 일관성**: 기존 문서의 마크다운 스타일 유지
5. **최소 범위**: 변경된 부분만 수정, 불필요한 전면 재작성 금지
6. **CLAUDE.md 동기화**: 주요 패턴, 포트, 서비스, Redis 키 변경 시 CLAUDE.md도 함께 업데이트
