---
name: update-docs
description: 코드 변경사항을 분석하여 관련 docs 문서를 업데이트합니다.
allowed-tools: Read, Edit, Grep, Glob, Bash
---

## 문서 업데이트 실행

$ARGUMENTS가 있으면 해당 내용을 업데이트 범위/대상으로 반영한다.

### 1단계: 변경사항 파악

다음을 병렬로 실행하여 변경 내용을 파악한다:
- `git diff HEAD --name-only` (staged + unstaged 변경 파일 목록)
- `git diff HEAD` (staged + unstaged 전체 변경 내용)
- `git status` (untracked 새 파일 확인, `-uall` 플래그 사용 금지)

### 2단계: 업데이트 대상 문서 결정

`docs/Documentation-Checklist.md`의 **변경 유형별 업데이트 대상** 테이블을 따른다. 주요 패턴, 포트 할당, Redis 키, 시스템 구조 등이 변경된 경우 CLAUDE.md도 함께 업데이트한다.

### 3단계: 대상 문서 읽기

업데이트 대상 문서를 모두 읽어 현재 내용을 파악한다.

### 4단계: 문서 업데이트

`docs/Documentation-Checklist.md`의 **문서 작성 원칙**을 따른다. 변경된 부분만 수정하고 불필요한 재작성은 하지 않는다.

### 5단계: 사용자에게 변경 내용 요약

업데이트한 문서 목록과 각 문서의 변경 내용을 간략히 보고한다.
