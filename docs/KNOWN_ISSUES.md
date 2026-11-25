# 알려진 문제 (KNOWN_ISSUES)

상태 표기: OPEN(미해결) / MITIGATED(완화, 근본 해결 아님) / BLOCKED(외부 조건 필요) / FIXED(이번 작업에서 해결, 기록용)

| ID | 상태 | 우선 | 내용 | 근거 / 다음 조치 |
|---|---|---|---|---|
| K-001 | MITIGATED (v0.4) | P1 | Custom Drive 중·고게인(Drive ≥ 50)에서 OS 2x·44.1/48 kHz 앨리어싱이 Codex보다 20–35 dB 높음 (D50 H50: −51 dB vs −74 dB, 3953 Hz −7.2 dBFS 스트레스 신호) | E-013. 4x에서는 D50 −92.8 dB, D100은 8x에서도 −48 dB. 사용자 안내 "Drive 60 이상 4x 권장". 다음: 1차 ADAA(K₂·asinh 경로는 닫힌 형식 존재) 또는 고게인 전용 내부 배율, 하프밴드 탭 증가 실험 |
| K-002 | OPEN | P1 | 같은 동작점(소신호 이득·onset 일치)에서 Claude v2의 어택 압축이 Codex보다 1.5–2배 강함 (예: Codex D75 H50 ↔ Claude D65 H0: −18.3 vs −9.0 dB) | E-013 매칭 비교. 바이어스 깊이·κ 재조정 후보. 청감으로 결정해야 함 |
| K-003 | OPEN | P1 | 프리셋 02/03/05는 v2에 맞게 레벨만 맞췄고 음색은 v1과 40–50 % 다름 | PARAMETER_SPECIFICATION §2. 사용자 청취 확인 필요 |
| K-004 | BLOCKED | P0 | macOS auval / Logic Pro 동작 미확인 | 사용자 Mac에서 `scripts/macos_build_install_verify.sh` 실행 필요. 클라우드·Linux VM에서는 macOS 빌드 불가 |
| K-005 | MITIGATED | P1 | NAM 모델 샘플레이트 ≠ 호스트 샘플레이트이면 NAM 블록이 바이패스됨(오음 출력 방지) | D-013. 실시간 리샘플러(폴리페이즈, 지연 보고) 미구현 → DEFER |
| K-006 | OPEN | P2 | Custom Drive 진단 값(입력 피크, 클리퍼 입력/Vth, 게인 압축, 적용 값)은 코어 API만 있고 플러그인 UI 표시 없음 | `CustomDrive::readDiagnostics()`. UI 패널은 향후 |
| K-007 | OPEN | P2 | 빌드 ID는 CMake configure 시점의 git 해시. configure 없이 재빌드하면 이전 해시가 남음 | 릴리스 빌드는 항상 새 build 디렉터리에서 configure |
| K-008 | OPEN | P2 | Undo/Redo, Tap Tempo UI, 값 직접 입력은 미구현 | 구현된 것처럼 보이는 UI는 두지 않음 |
| K-009 | OPEN | P2 | 모든 음향 비교는 합성 신호(Karplus-Strong DI, 사인)와 SPICE 시뮬레이션 기준. 실제 DI 녹음·하드웨어 측정 없음 | 사용자 DI 녹음(험버커/싱글, 강약 피킹, 볼륨 롤오프) 제공 시 같은 하니스로 재측정 |
| K-010 | OPEN | P2 | ABX 세트는 생성만 했고 청취 평가는 수행하지 않음(AI는 청취할 수 없음) | `audio/compare_drive_v2/abx` (Mac 동기화본). 정답 키는 청취 후에만 열 것 |
| K-011 | FIXED | P0 | 호스트 스레드가 SlotParameter를 통해 Effect*에 접근(수명 경쟁) | seqlock 메타데이터, 메시지 스레드 적용. TSan 경고 0 |
| K-012 | FIXED | P0 | 튜너 링버퍼 데이터 레이스 | atomic<float> relaxed |
| K-013 | FIXED | P1 | 딜레이/리버브 바이패스 시 꼬리가 잘림 | 트레일 처리(입력만 페이드, 이펙트는 계속 처리) |
| K-014 | FIXED | P1 | Custom Drive v1이 실사용 레벨에서 과도한 포화·압축·앨리어싱 | 매핑 v2 (E-013) |
| K-015 | OPEN | P1 | (R3) Windows 빌드와 macOS/Windows/Linux 간 렌더 출력 일치를 검증하지 않음 | 플랫폼별 골든 렌더 비교(허용 오차 명시) 추가 |
| K-016 | OPEN | P2 | (R4) `ScopedNoDenormals`가 x86(MXCSR)·aarch64(FPCR)만 지원, ARMv7/Cortex-M(FPSCR FZ) 경로 없음 | 하드웨어 후보 확정 시 추가 |
| K-017 | OPEN | P2 | (R4) 이펙트별 메모리 사용량(prepare 할당 바이트) 측정·보고 없음 | `Effect::memoryBytes()` 또는 할당 카운터로 벤치마크에 추가 |
| K-018 | OPEN | P3 | (R4) NAM(Eigen·예외·힙)과 JSON 파서는 Cortex-M에 부적합, 연구용 정적 다이오드 클리퍼는 샘플당 double | NAM은 선택 모듈 유지, MCU 타깃은 프리셋 바이너리 포맷·float 경로 별도 |
| K-019 | MITIGATED (v0.4: Custom Drive `model` 파라미터, 없으면 v2, v1 선택 가능. v0.1 JSON 파일은 자동 구분 불가) | P1 | (R6) 프리셋에 이펙트 모델 버전이 없어 Custom Drive v2가 기존 프리셋 소리를 바꿈(팩토리 프리셋은 레벨만 재조정, v1 경로는 코드에만 존재) | 블록별 `model_version`(없으면 1) + Custom Drive v1/v2 선택. v0.1 미출시라 사용자 피해는 없음 |
| K-020 | OPEN | P1 | (R6) 팩토리 프리셋 렌더 회귀(골든 출력) 테스트 없음 — 소리 변화가 자동으로 검출되지 않음 | 프리셋별 고정 입력 렌더 해시/ESR 비교 테스트 |
| K-021 | OPEN | P2 | (R6) 앰프 Model 같은 Choice 파라미터에 항목을 추가하면 호스트 정규화 값이 바뀌어 DAW 자동화가 다른 모델을 가리킬 수 있음(프리셋 JSON은 영향 없음) | 예비 슬롯 확보 또는 새 앰프는 새 type id 정책 결정 |
| K-022 | OPEN | P1 | (시각화) Waveform/Spectrum·HOT/CLIP 표시·Effects/Visualize 전환을 Logic에서 확인하지 않음. Linux 오프스크린 렌더(`docs/images/ui_visualize_snapshot.png`)와 pluginval GUI 테스트만 수행 | 사용자 Logic 확인 |
| K-023 | OPEN | P2 | 스펙트럼은 Hann 창이라 빈 사이 주파수에서 최대 1.42 dB 낮게 읽힘(스캘럽 손실). 저역 표시점은 빈 보간, 그 외는 피크 유지 | 정밀 레벨 측정은 미터 사용. 필요 시 Flat-top 옵션 |
| K-024 | OPEN | P2 | 캐비닛 IR 파일이 없으면 프리셋 소리가 내장 시뮬레이션으로 바뀜(경고 표시). IR을 프리셋과 함께 옮기는 "번들 내보내기"는 없음 | 프리셋 폴더/ir/ 또는 ~/Music/MFX Claude/IR에 두면 자동 복원 |
| K-025 | OPEN | P2 | 새 IR 상태 형식(`mfx-ir:1`)은 v0.2.0 이하 빌드에서 경로로 읽히지 않음(경고 후 내장 시뮬레이션). 이전 형식(경로 문자열)은 새 빌드에서 그대로 읽힘 | 하위 호환은 유지, 상위 호환 없음 |
| K-026 | FIXED (gap v1) / OPEN (v0.4) | P1 | gap v1은 사용자 Mac에서 빌드·테스트 78/78·auval PASS(`tests/results/macos_verify_20261124-174619.txt`). v0.4 후보는 macOS AU 빌드·auval 미확인(작업 환경에서 macOS 툴체인 실행 불가). 코어·테스트는 사용자 Mac의 Linux arm64 VM에서 78/78 PASS, `~/multi_effects_claude` 반영 완료 | 사용자 Mac에서 `scripts/macos_build_install_verify.sh` |
| K-027 | OPEN | P2 | Custom Drive `v2 + ADAA` 경로는 Python 미러·parity 대상이 아님(v2만 parity) | 필요 시 `mfx_dsp.py`에 ADAA 경로 추가 |
| K-028 | FIXED (v0.5, `adaa_comp`) | P2 | ADAA는 단마다 오버샘플 반 샘플 군지연 추가(정수 지연 보고에 미반영). Mix < 100 %에서 고역 위상 차 미세 | 분수 지연 보정 검토 |
| K-029 | MITIGATED (v0.5 M2: PI·6V6·Sag·NFB·OT·부하) | P1 | AB763 M1: 위상반전단·6V6·Sag·NFB·OT·톤스택 부하 미모델, 회복단 캐소드 조건 가정, 입력 0 dBFS = 1 V 가정. 실제 앰프와 비교 측정 없음 | M2, 실측 |
| K-030 | OPEN | P2 | ADAA 경로는 샘플당 double 연산(Cortex-M 등 단정밀 FPU 하드웨어에서 비용 큼) | 하드웨어 후보 확정 시 float 버전 검토 |
| K-031 | PARTIAL (v0.4 Mac 빌드·86/86·auval PASS, Logic GUI 미확인) | P1 | v0.4 후보의 Logic 실사용(AB763 선택·Model 전환·프리셋) 미확인 | 사용자 Mac 1회 검증 |
| K-032 | OPEN | P1 | AB763 M2의 6V6 Koren 파라미터·PI 전달곡선·PI 입력 분압(0.14)·그리드 전도 계수는 문헌/근사값. SPICE·실측 비교 없음 | SPICE 기준 회로, 실측 |
| K-033 | OPEN | P2 | AB763 M2 1x 오버샘플링에서는 피드백 대역 제한(0.2·fs)으로 고역 응답이 2x 이상과 다름 | 2x 이상 사용(기본 2x) |
| K-034 | OPEN | P2 | AB763 M2는 M1보다 같은 볼륨에서 약 6 dB 큼(출력 스케일 차) | Level로 보정, 청취 후 정규화 결정 |
| K-035 | OPEN | P2 | Premium Plate 초기 에코 밀도(NED 50 ms 0.54)가 Legacy FDN(0.97)보다 낮음 — 플레이트 특성인지 청취 필요 | 입력 확산 단 추가 검토 |
| K-036 | OPEN | P2 | 구형 Claude AU(MultiEffects, Mfxp)가 Logic 목록에 중복 표시 | 플러그인 관리자에서 비활성화(BUILD_GUIDE §2-1), 필요 시 이동 스크립트 |
| K-037 | OPEN | P1 | v0.5 후보의 macOS 네이티브 빌드·auval·Logic 미확인 | 사용자 Mac 1회 |
| K-038 | OPEN | P2 | "Premium Audio Quality Standard"·"기술 연구 통합 보고서" 원문 미확보(지시서 요약만 반영) | 원문 제공 시 AUDIO_QUALITY_TARGETS/TECHNICAL_RESEARCH 갱신 |
| K-039 | OPEN | P2 | Tape Echo 메모리 192 kHz 5 MiB(최장 3.6 s × 3헤드 대비) — 하드웨어에서는 큼 | 하드웨어 빌드에서 최대 시간 제한 또는 테이프 버퍼 다운샘플 |
| K-040 | OPEN | P2 | BBD는 단일 4096단 칩 모델 → 300 ms 이상에서 강한 앨리어싱(800 ms, 3 kHz 톤이 440 Hz로 접힘) | 의도된 특성이지만 고음질 장지연용 "다중 칩" 옵션 검토 |
| K-041 | OPEN | P2 | FET Compressor의 비율별 임계·니·All 모드·Character 계수는 추정값(1176 실측 없음) | 실기 또는 공개 측정 자료와 비교 |
| K-042 | OPEN | P2 | Tape/BBD/FET 청취 평가·실기 비교 없음, parity 미러 없음 | 사용자 청취(K 항목) |
| K-043 | OPEN | P3 | Tape Echo는 모노 테이프(스테레오 입력은 합산), 스테레오는 헤드 패닝(Spread)으로만 | 필요 시 듀얼 테이프 |
| K-044 | OPEN | P3 | Delay `time_r_mode = Free`는 템포 싱크 시에도 ms 값 사용 | R 싱크 분할 파라미터 추가 검토 |
| K-045 | OPEN | P1 | v0.6 macOS 네이티브 빌드·auval·Logic 미확인 (K-037 포함) | 사용자 Mac `scripts/macos_build_install_verify.sh` |
