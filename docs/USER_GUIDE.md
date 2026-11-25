# 사용자 가이드

## 화면
* **상단**: 프리셋 선택 · Save/Load(JSON) · Parallel A/B 토글 · 입력 모드(Auto / Mono L / Mono L+R / Stereo)
  · IN/OUT 게인 · A/B 밸런스 · 입출력 미터(피크·RMS·클립 LED) · CPU · 지연.
* **신호 체인**: PRE → (CHAIN 또는 LANE A / LANE B) → POST.
  * `+` : 해당 레인 끝에 이펙트 추가(카테고리 메뉴)
  * 클릭: 선택(아래 파라미터 패널 표시) · 더블클릭: 바이패스 · 드래그: 순서 변경/레인 이동 · 우클릭: 메뉴(바이패스, 삭제, 이동)
  * 블록 하단 숫자는 해당 이펙트의 지연(샘플).
* **파라미터 패널**: 노브(더블클릭 = 기본값, 값 직접 입력 가능), 선택 상자, 토글.
  Cabinet은 *Load IR*, NAM Amp는 *Load .nam*, DDSP Drive는 *Load knob LUT* 버튼이 있습니다. Tuner는 음정·센트를 표시합니다.

## Custom Drive Model (v0.4)
* **v2**(기본): v0.2부터의 소리. **v2 + ADAA**: 같은 소리에 고음·고게인의 금속성 앨리어싱을 줄임(CPU 약 20 % 증가) — Drive 60 이상에서 권장.
* **v1 (legacy)**: v0.1 소리. v0.1에서 저장한 JSON 프리셋은 불러온 뒤 Model을 v1로 바꾸세요.

## AB763 Normal (M1) 앰프
블랙페이스 계열 Normal 채널 회로 모델: Volume / Treble / Bass(0–10) + Level·Oversampling. Volume 5 근처까지 클린, 7 이상에서 브레이크업.
Cabinet 블록(IR 또는 내장 시뮬레이션)을 뒤에 두세요(팩토리 프리셋 "Blackface Normal").

## v0.5 추가
* **AB763 Normal Model**: M2(새 기본) = 위상반전단·6V6 출력관·전원 Sag·네거티브 피드백·스피커 부하 반응. 볼륨을 올릴수록 출력단이 눌리고,
  세게 칠 때 순간적으로 전압이 처집니다. 예전 프로젝트/프리셋(06)은 M1로 열립니다.
* **Custom Drive ADAA Compensation**: Model이 "v2 + ADAA"일 때 고역 손실과 Mix 사용 시 위상 차이를 보정(새 인스턴스 기본 On).
* **Premium Plate**: 새 리버브. Decay(RT60), Pre-Delay, Size, Damping(고역 감쇠), Bass Decay(저역 잔향 배수), Crossover, Diffusion,
  Mod Depth/Rate, Early, Low Cut, Width, Mix. 팩토리 프리셋 07 "Blackface M2 Plate".

## Visualize (파형 · 스펙트럼)
상단 **Effects / Visualize** 버튼으로 화면을 전환합니다. Visualize를 열었을 때만 분석이 동작하며, 소리에는 영향이 없습니다.
* 표시: Input(체인 입력, 입력 게인 적용 후) / Output(체인 출력) / Overlay. 입력은 체인 지연만큼 늦춰 출력과 맞춰 그립니다.
* 채널: Mono(L+R) / Left / Right / Stereo L/R. Time(5 ms–2 s), Amp(1–32배) 줌, Pause/Resume.
* 스펙트럼: FFT 2048/4096, Hann 창, 로그 주파수축, dBFS(풀스케일 사인 = 0 dBFS). 빈 사이 주파수는 최대 1.4 dB 낮게 보일 수 있습니다.
* 아래 상태줄: 샘플레이트, 입·출력 채널, 체인 지연, 분석 데이터 누락(dropped) 수.

## 입력 레벨
* IN 미터: 체인 입력(입력 게인 적용 후). 흰 눈금 = 공칭 −20 dBFS RMS, LED 주황 = −3 dBFS(HOT), 빨강 = 0 dBFS(CLIP).
* "Source peak": 입력 게인 적용 전 호스트/인터페이스 입력 피크. 비정상 샘플(NaN/Inf)이 오면 0으로 막고 표시합니다.

## 캐비닛 IR과 프리셋
프리셋에는 IR 파일 위치·이름·내용 확인값만 저장합니다(IR 샘플은 저장 안 함). 원래 위치에 없으면
① 프리셋 파일과 같은 폴더(또는 그 안의 `ir/`, `IR/`) ② `~/Music/MFX Claude/IR` 에서 같은 이름을 찾고 내용이 같은지 확인합니다.
이름은 같은데 내용이 다르면 경고와 함께 로드하고, 찾지 못하면 내장 캐비닛 시뮬레이션으로 대체하고 경고합니다.

## 자동화 · MIDI
* 호스트 파라미터: `Input Gain`, `Output Gain`, `Parallel A/B Mix`, 슬롯 1–16 각각 `Bypass` + 파라미터 16개.
  이름은 슬롯의 이펙트를 따라갑니다(예: `S2 Drive`).
* MIDI CC 매핑은 프리셋 JSON의 `midi` 배열로 정의합니다(예: CC 64 → 슬롯 바이패스). UI 편집기는 아직 없습니다.

## Custom Drive 노브 (매핑 v2)
| 노브 | 하는 일 |
|---|---|
| Drive | 클리퍼로 들어가는 이득(−6…+30 dB). 0 근처는 보통 험버커 레벨에서 거의 클린, 올릴수록 일찍·많이 일그러짐 |
| Headroom | 포화가 시작되는 레벨(−18…0 dBFS). 낮추면 작게 쳐도 압축·일그러짐, 높이면 클린 여유. 출력 레벨도 절반쯤 함께 변함 |
| Tight | 클리핑 전 저역 컷(30–900 Hz). 저음 뭉침과 상호변조 감소 |
| Crunch | 포화 곡선의 단단함. 낮으면 포화 뒤에도 피킹 강약이 많이 남고, 높으면 단단하고 압축된 느낌 |
| Grain | 질감: 비대칭(짝수 배음)과 클리핑 전 고역 강조 |
| Mid Character | 클리핑 전 중역 강조/감쇠(+ 클리핑 후 일부) |
| Tone | 클리핑 후 밝기(틸트)와 고역 거칠기(fizz) 필터 |
| Mix / Level / Oversampling | 병렬 클린 / 출력 / 앨리어싱 억제(높을수록 CPU 증가, 지연 31–41 샘플) |

* **Drive 60 이상에서는 Oversampling 4x를 권장합니다.** 2x에서는 고음 연주 시 금속성 앨리어싱이 남을 수 있습니다(KNOWN_ISSUES K-001).
* v0.1에서 저장한 프리셋의 Custom Drive는 v2에서 소리가 달라집니다(더 깨끗하고 작게). 팩토리 프리셋은 v2에 맞춰 다시 조정했습니다.

## 팩토리 프리셋
Clean Studio, Crunch Rhythm, High Gain Lead, Ambient Swells(병렬 A: 테이프 딜레이 / B: 앰비언트 리버브),
Custom Drive Showcase. 레벨은 합성 기타 신호로 약 −14 ~ −20 dBFS RMS가 되도록 맞췄습니다.

## 알아둘 점
* 앰프·캐비닛·드라이브 모델은 특정 제품의 복제가 아니며 실제 장비와 비교 검증하지 않았습니다.
* NAM 모델이 엔진 샘플레이트와 다르면 바이패스되고 상태 문구가 표시됩니다(실시간 리샘플링 미구현).
* 외부 IR/NAM 파일의 사용 권한은 사용자 책임입니다.
