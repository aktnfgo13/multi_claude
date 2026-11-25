# 통합 고도화 완료 보고서 (INTEGRATION_REPORT) — 2026-11-24

* 메인: MFX Claude v0.2.0, 브랜치 `integration/reference-project-review` (기준 태그 `baseline-v0.1`)
* 참고: Codex 버전 `~/multi_effects` (읽기 전용, 수정·복사 없음)
* 작업 환경: 클라우드 Linux x86_64(빌드·테스트·측정), 사용자 Mac 폴더는 브리지로 동기화만.
  **macOS 빌드·auval·Logic은 이 환경에서 실행할 수 없어 사용자 실행이 필요합니다.**

## A. 비교표 (요약 — 전체는 REFERENCE_PROJECT_COMPARISON.md)

| 기능 | Codex | Claude 기존 | 최종 | 이유 |
|---|---|---|---|---|
| Custom Drive 게인 구조 | D0 거의 클린 | D0부터 과포화 | v2 재설계 | 측정상 과포화·과압축 (REWRITE) |
| Headroom 의미 | 포화+레벨 | 포화만 | 50 % 보상 | 연주감 균형 (ADOPT 개념) |
| 클리핑 곡선 | 소프트 + 바이어스 | |z|^p | 해석적 니 + κ + β | 고차 배음·압축 감소 (REWRITE) |
| 오버샘플러 | 긴 Blackman, 지연 64 | 하프밴드, 지연 31–41 | 유지 | CPU 4배·지연 절반 (KEEP) |
| 호스트 파라미터 | 고정 | 동적, 수명 경쟁 | seqlock | RT 안전 (IMPROVE) |
| 테일 / 튜너 | — | 잘림 / 레이스 | 트레일 / atomic | (IMPROVE) |
| 앰프 저역 정밀도 | double | float | float | 오차 ≤ 0.17 dB (KEEP) |
| NAM SR | — | 바이패스 | 바이패스 | 리샘플러 (DEFER) |
| 시각화·Input System | 있음 | 없음 | 없음 | 이식 후보 (DEFER) |
| 플러그인 ID | Mefx/Mfx6 | Mfxp/Mfx1 | MfxL/McL1 | 덮어쓰기 방지 |

## B. 변경 사항 (커밋)
1. `RT stability` — SlotParameter seqlock 메타데이터(호스트 스레드에서 Effect* 미접근), 튜너 atomic 링버퍼,
   딜레이·리버브 트레일, 동시성 테스트 2종, ASan/UBSan/TSan 빌드 옵션(`MFX_SANITIZE`).
2. 빌드 식별 — 제품명 "MFX Claude", MfxL/McL1, com.mfxlab.mfxclaude, 창 우하단 버전·커밋, `mfx_render --version`.
3. `Custom Drive mapping v2` — 게인 스테이징, 해석적 니 곡선, κ 잔여 기울기, β 동작점, 클리핑 전 7.2 kHz LPF,
   엠퍼시스 축소, v1 보존(`customDriveMappingV1`), Python 미러·parity 갱신, 테스트 명세 갱신(D-018).
4. 진단 — `CustomDrive::readDiagnostics()`(입력 피크/RMS, 클리퍼 입력/Vth, 출력 피크, 게인 압축, 적용 값), 기본 off.
5. 공통 비교 하니스 `apps/mfx_compare_drive`, 그림 스크립트, 프리셋 영향 도구, 팩토리 프리셋 02/03/05 재조정.
6. 연구 — DDSP loss 설정 파일·Envelope loss·train/val/test·지연 정렬, 노브 매핑 방법 비교, SPICE 공차 스윕.
7. 문서 — PARAMETER_SPECIFICATION, REFERENCE_PROJECT_COMPARISON, KNOWN_ISSUES, INTEGRATION_REPORT 신규,
   DSP_SPECIFICATION·EXPERIMENT_LOG(E-012~018)·DECISION_LOG(D-016~029)·BUILD_GUIDE·USER_GUIDE·BENCHMARK_PROTOCOL·
   RESEARCH_PLAN·DEVELOPMENT_PLAN·README·CLAUDE.md·THIRD_PARTY_NOTICES 갱신. `scripts/macos_build_install_verify.sh` 추가.

## C. 성능표 (같은 Linux 기계, 같은 빌드)

| 항목 | Codex | Claude 전(v0.1) | Claude 후(v0.2) |
|---|---|---|---|
| Custom Drive CPU 1x/2x/4x (ns/샘플, 모노) | 157 / 668 / 1366 | 74 / 207 / 395 | 71 / 189 / 353 |
| Custom Drive 지연 2x/4x (샘플) | 64 / 64 | 31 / 39 | 31 / 39 |
| 앨리어싱 D50 H50 2x, 48 kHz | −74.4 dB | −22.8 dB | −51.3 dB |
| 앨리어싱 D0 H50 2x, 48 kHz | −83.6 dB | −51.8 dB | −92.9 dB |
| 전체 체인 실시간 배수 블록 32/128/512 | 측정 안 함(체인 구성 다름) | 14.9 / 14.9 / 15.1 | 15.1 / 15.0 / 14.7 |
| 오디오 스레드 힙 할당 | — | 0 | 0 |

## D. 상태

| 항목 | 상태 | 근거 |
|---|---|---|
| 기준 보존(태그·브랜치·Mac 스냅샷), Codex 무수정 | PASS | baseline-v0.1, `_snapshots/` |
| C++ 테스트 70개 | PASS | `tests/results/cpp_tests.json` |
| ASan/UBSan, TSan | PASS (경고 0) | `tests/results/sanitizer_*.txt` |
| Python/C++ parity | PASS | `tests/results/parity_report.json` |
| pluginval strictness 7 + GUI (VST3, Linux) | PASS | `pluginval_vst3_linux_strict7_v2.txt` |
| 플러그인 ID 분리 | PASS(코드) / NOT TESTED(Mac 설치) | BUILD_GUIDE §2 |
| macOS arm64 AU/VST3/Standalone 빌드 | NOT TESTED (v0.1은 사용자 빌드 성공) | 사용자 실행 필요 |
| auval | BLOCKED | macOS 필요 — 스크립트 제공 |
| Logic Pro 체크리스트 | BLOCKED | 사용자 |
| Custom Drive 공통 벤치마크(동일 노브·매칭 동작점, 44.1/48/96 kHz) | PASS | E-013 |
| Custom Drive 게인 스테이징·클린업 | PASS(측정) / NOT TESTED(청취) | E-013 |
| Custom Drive 고게인 앨리어싱·어택 압축 | PARTIAL | K-001, K-002 |
| 진단 기능 | PARTIAL (코어 API PASS, UI NOT IMPLEMENTED) | K-006 |
| 프리셋 영향 문서화·재조정 | PASS(레벨) / NOT TESTED(청취) | PARAMETER_SPECIFICATION §2 |
| 트레일 / 튜너 레이스 / 파라미터 수명 | PASS | E-012 |
| NAM 리샘플링 | NOT IMPLEMENTED (DEFER) | K-005 |
| ADAA | NOT IMPLEMENTED (DEFER) | D-023 |
| ABX 청취 | NOT TESTED (세트 생성만) | K-010 |
| DDSP loss 설정·Envelope loss·분할·지연 정렬 | PASS(기능) / 정확도 개선 없음 | E-016 |
| 노브 매핑 방법 비교 | PASS | E-017 |
| SPICE 공차 스윕 | PASS(시뮬레이션) | E-018 |
| Gray-box / Architecture Search | NOT IMPLEMENTED (DEFER) | D-025 |
| Undo/Redo, Tap Tempo UI | NOT IMPLEMENTED | K-008 |
| Windows 빌드 | NOT TESTED | — |

## E. 실행 결과 (명령)
```
./build/mfx_tests                                   → 70 passed, 0 failed
build-asan / build-tsan mfx_tests                   → 경고 0
python3 tests/parity/run_parity.py                  → PASS
xvfb-run pluginval --strictness-level 7 (MFX Claude.vst3) → SUCCESS
./build/mfx_compare_drive <out> [--quick] [--fs] [--os] → tests/results/compare_drive/*
python3 tools/benchmark/preset_mapping_impact.py    → 프리셋 레벨 차 ≤ 0.2 dB
python3 research/ddsp/train_ddsp.py --loss-config … → E-016
python3 research/parameter_mapping/compare_mapping_methods.py → E-017
python3 research/spice/run_spice.py tolerance …     → E-018
```

## F. 사용자 확인 항목
1. Mac에서 `cd ~/multi_effects_claude && bash scripts/macos_build_install_verify.sh` 실행 → 출력된 로그 파일
   (`tests/results/macos_verify_*.txt`, `auval_*.txt`) 결과 알려주기.
2. Logic 체크리스트 10항목(BUILD_GUIDE §2).
3. 청취: `audio/compare_drive_v2/abx/trial_NN`의 A·B·X 중 X가 어느 쪽인지 기록한 뒤 정답 키 열기. 렌더는 합성 기타 신호입니다.
4. Custom Drive v2 감각: Drive 0–30에서 기타 볼륨을 줄였을 때의 클린업, Drive/Headroom 역할 구분, 고게인 fizz.
5. 프리셋 02/03/05가 의도한 느낌인지(레벨만 맞춤).
6. Headroom 부분 보상(D-019) 방향이 맞는지 — 완전 보상(v1식)과 Codex식 중 선호.

## G. 향후 계획
1. K-001/K-002: 1차 ADAA(해석적 곡선이라 적용 가능) → 같은 하니스로 Codex와 재비교. 
2. 실제 DI 녹음·Jan Ray 복각 루프백 측정으로 하니스·DDSP 재실행(합성 신호 한계 해소).
3. NAM 리샘플러, 진단 UI, Undo/Redo.
4. Codex 고유 기능 이식 검토(시각화, Input System, preset 내장 IR) — 재구현, 사용자 승인 후.
5. Gray-box / Architecture Search는 실측 데이터 확보 후.

---
# 추가: Codex 기능 격차 해소 v1 (2026-11-24, 기준 커밋 3f823fa, **미커밋**)

| 항목 | 상태 | 근거 |
|---|---|---|
| 분석 탭(SPSC) + 워커 | PASS | `integration.analysis_*` 5개, TSan/ASan 경고 0 |
| 분석 ON/OFF 출력 동일 | PASS | `analysis_tap_does_not_change_output`(비트 동일) |
| 콜백 할당/해제 0 (탭 ON, 큐 넘침, 토글) | PASS | `performance.no_heap_allocation_or_free_with_analysis_tap` |
| Waveform / Spectrum 계산 | PASS | 정렬 오차 < 1e-4, 빈 중심 사인 −6 dBFS ±0.2 dB |
| Waveform / Spectrum UI | PARTIAL | Linux 오프스크린 렌더 확인, pluginval GUI 통과 / Logic NOT TESTED |
| 입력 시스템 보완(원본 피크, HOT, 비유한 가드, 공칭 눈금) | PASS(코어) / NOT TESTED(Logic 표시) | `input_meters_source_peak_hot_and_nonfinite_guard` |
| 캐비닛 IR 참조·이동·누락·해시 | PASS | `cabinet_ir_reference_survives_moves_and_missing_files` |
| 팩토리 프리셋 5종 출력(골든 렌더) | PASS — 변경 전과 비트 동일 | `mfx_render` 렌더 cmp |
| Custom Drive v2 회귀 | PASS | audio custom_drive 13개 + 골든 렌더 |
| C++ 전체 | PASS 78개 | `tests/results/cpp_tests.json` |
| pluginval strictness 7 (VST3 Linux) | PASS | `pluginval_vst3_linux_strict7_v3_analysis.txt` (스펙트럼 표시점 미세 조정 전 빌드) |
| arm64(사용자 Mac의 Linux VM) 코어+테스트 | PASS 78/78 | `tests/results/arm64_linux_vm_tests.txt` |
| macOS AU 후보 빌드 / auval | NOT TESTED | macOS 툴체인 필요(K-026) |

---
# 추가: v0.4 Sound Engine Expansion (2026-11-25)

| 항목 | 상태 | 근거 |
|---|---|---|
| 플러그인 ID 확인·고정 (MfxL/McL1), 구형·Codex AU 보존 | PASS | Mac `auval -a`: 3개 공존 (`macos_verify_20261124-174619.txt`) |
| Custom Drive v1 = v0.1 출력 | PASS (비트 동일 4설정) | `/tmp` v0.1 빌드(e1515b2) vs `--set model=0` cmp |
| Custom Drive v2 = 기존 출력 | PASS (팩토리 01–05 비트 동일) | 골든 렌더 cmp |
| v2 + ADAA 앨리어싱 개선 | PASS (2x D75/D100 −20 dB) | `custom_drive_model_parameter_and_versions`, `mfx_compare_drive --claude-model 2` |
| AB763 톤스택 디지털 = 아날로그 | PASS (±0.35 dB) | `ab763_tone_stack_*` |
| AB763 동작점·볼륨 브레이크업·SR 독립 | PASS | `ab763_*` 4개 |
| 프리셋 저장·복원 (amp + cabinet, model 없는 프리셋 → v2) | PASS | `ab763_preset_roundtrip_with_cabinet`, `custom_drive_preset_without_model_loads_v2`, 팩토리 06 로드 |
| 콜백 할당/해제 0 (ADAA, 모델 전환, AB763) | PASS | `no_heap_allocation_or_free_ab763_and_custom_drive_adaa` |
| C++ 전체 | PASS 86/86 | `tests/results/cpp_tests.json` |
| ASan/UBSan (audio) | PASS 0건 | `sanitizer_asan_v04_audio.txt` |
| pluginval strictness 7 (VST3 Linux) | PASS | `pluginval_vst3_linux_strict7_v04.txt` |
| arm64 (사용자 Mac의 Linux VM) 코어+테스트 | PASS 86/86 | `tests/results/arm64_linux_vm_tests_v04.txt` |
| macOS AU 빌드·auval·Logic | NOT TESTED | 사용자 Mac 1회 검증 대기(K-031) |
