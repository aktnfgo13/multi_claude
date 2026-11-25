# 알려진 문제 (KNOWN_ISSUES)

상태 표기: OPEN(미해결) / MITIGATED(완화, 근본 해결 아님) / BLOCKED(외부 조건 필요) / FIXED(이번 작업에서 해결, 기록용)

| ID | 상태 | 우선 | 내용 | 근거 / 다음 조치 |
|---|---|---|---|---|
| K-001 | OPEN | P1 | Custom Drive 중·고게인(Drive ≥ 50)에서 OS 2x·44.1/48 kHz 앨리어싱이 Codex보다 20–35 dB 높음 (D50 H50: −51 dB vs −74 dB, 3953 Hz −7.2 dBFS 스트레스 신호) | E-013. 4x에서는 D50 −92.8 dB, D100은 8x에서도 −48 dB. 사용자 안내 "Drive 60 이상 4x 권장". 다음: 1차 ADAA(K₂·asinh 경로는 닫힌 형식 존재) 또는 고게인 전용 내부 배율, 하프밴드 탭 증가 실험 |
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
| K-019 | OPEN | P1 | (R6) 프리셋에 이펙트 모델 버전이 없어 Custom Drive v2가 기존 프리셋 소리를 바꿈(팩토리 프리셋은 레벨만 재조정, v1 경로는 코드에만 존재) | 블록별 `model_version`(없으면 1) + Custom Drive v1/v2 선택. v0.1 미출시라 사용자 피해는 없음 |
| K-020 | OPEN | P1 | (R6) 팩토리 프리셋 렌더 회귀(골든 출력) 테스트 없음 — 소리 변화가 자동으로 검출되지 않음 | 프리셋별 고정 입력 렌더 해시/ESR 비교 테스트 |
| K-021 | OPEN | P2 | (R6) 앰프 Model 같은 Choice 파라미터에 항목을 추가하면 호스트 정규화 값이 바뀌어 DAW 자동화가 다른 모델을 가리킬 수 있음(프리셋 JSON은 영향 없음) | 예비 슬롯 확보 또는 새 앰프는 새 type id 정책 결정 |
| K-022 | OPEN | P1 | (시각화) Waveform/Spectrum·HOT/CLIP 표시·Effects/Visualize 전환을 Logic에서 확인하지 않음. Linux 오프스크린 렌더(`docs/images/ui_visualize_snapshot.png`)와 pluginval GUI 테스트만 수행 | 사용자 Logic 확인 |
| K-023 | OPEN | P2 | 스펙트럼은 Hann 창이라 빈 사이 주파수에서 최대 1.42 dB 낮게 읽힘(스캘럽 손실). 저역 표시점은 빈 보간, 그 외는 피크 유지 | 정밀 레벨 측정은 미터 사용. 필요 시 Flat-top 옵션 |
| K-024 | OPEN | P2 | 캐비닛 IR 파일이 없으면 프리셋 소리가 내장 시뮬레이션으로 바뀜(경고 표시). IR을 프리셋과 함께 옮기는 "번들 내보내기"는 없음 | 프리셋 폴더/ir/ 또는 ~/Music/MFX Claude/IR에 두면 자동 복원 |
| K-025 | OPEN | P2 | 새 IR 상태 형식(`mfx-ir:1`)은 v0.2.0 이하 빌드에서 경로로 읽히지 않음(경고 후 내장 시뮬레이션). 이전 형식(경로 문자열)은 새 빌드에서 그대로 읽힘 | 하위 호환은 유지, 상위 호환 없음 |
| K-026 | OPEN | P1 | macOS AU 후보 빌드·auval 미확인(작업 환경에서 macOS 툴체인 실행 불가). 코어·테스트는 사용자 Mac의 Linux arm64 VM에서 78/78 PASS, `~/multi_effects_claude` 반영 완료 | 사용자 Mac에서 `scripts/macos_build_install_verify.sh` |
