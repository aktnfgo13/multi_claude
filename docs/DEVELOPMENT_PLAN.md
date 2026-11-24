# 개발 계획

## 목표
고음질 + 낮은 지연 + 효율적 연산 + 자유로운 파라미터 제어 + AI 기반 모델링 + 하드웨어 확장성.
우선순위: 음질 → 안정성 → 실시간 성능 → 파라미터 정확성 → 모듈화 → 하드웨어 이식성 → 사용 편의성 → AI 연구 확장성.

## 트랙 구조
* **제품 트랙**: DSP 코어 → 엔진 → AU/VST3/Standalone → UI → 프리셋 → NAM → 하드웨어.
* **연구 트랙**: 측정/SPICE → parity → DDSP → 노브 매핑 → Gray-box → Architecture Search.
* 두 트랙은 `mfx_core`에서만 만납니다. 연구 결과는 "새 이펙트"가 아니라 기존 블록의 구조/계수/LUT로 이전됩니다
  (예: D-010 WDF 이전, `ddsp_drive` + KnobLut).

## 우선순위와 진행 상황 (2026-11-24 통합 고도화 후)

| 단계 | 항목 | 상태 |
|---|---|---|
| P0 | 보존(스냅샷·태그·브랜치), Codex 비교, 회귀, RT 수정, 플러그인 ID 분리, Custom Drive 비교, 벤치마크 | 완료 — **AU 빌드·auval은 사용자 Mac 실행 대기(BLOCKED)** |
| P1 | Custom Drive 게인·Headroom·앨리어싱(v2), 테일, 진단, 프리셋 재조정 | 완료 / 고게인 앨리어싱·어택 압축 OPEN(K-001, K-002), NAM 리샘플링 DEFER, 진단 UI 미구현 |
| P2 | DDSP loss 설정·Envelope loss·분할, 노브 매핑 비교, SPICE 공차 | 완료(시뮬레이션 데이터) |
| P3 | Gray-box, Architecture Search, 임베디드 | 미착수(DEFER) |

## 다음 마일스톤
1. M1 (Mac, 사용자): `scripts/macos_build_install_verify.sh` → auval → Logic 체크리스트(BUILD_GUIDE §2) → 결과 로그 전달.
2. M1b (청취, 사용자): `audio/compare_drive_v2/abx` 블라인드 청취, 프리셋 02/03/05 확인, Custom Drive v2 노브 감각 피드백.
3. M2 (P1): K-001/K-002 — 1차 ADAA, 바이어스·κ 재조정, 결과를 같은 하니스로 Codex와 재비교.
4. M3 (측정): 실제 DI 녹음과 Jan Ray 복각 루프백 측정 → 하니스·DDSP 재실행.
5. M4 (제품): NAM 리샘플러, 진단 UI, Codex 고유 기능(스펙트럼 시각화, Input System) 이식 검토, Undo/Redo.
6. M5 (하드웨어): 후보 보드에서 `mfx_core` 크로스 빌드, 블록별 사이클 측정.

세부 상태표는 `docs/INTEGRATION_REPORT.md` (v0.1: `docs/COMPLETION_REPORT.md`).
