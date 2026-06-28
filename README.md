# portraitmod

Force portrait mode untuk GTA SA Android (SA-MP MPDE) via AML hook.

## Analisa

Dilakukan via Termux terhadap `libGTASA.so` (ARM32 Thumb, stripped):

| Fungsi | VA (offset) | Keterangan |
|---|---|---|
| `OS_ScreenGetWidth` | `0x268d3c` | Baca runtime struct → `[ptr+0]` |
| `OS_ScreenGetHeight` | `0x268d4c` | Baca runtime struct → `[ptr+4]` |
| `NVEventGetOrientation` | `0x27116c` | Baca `.bss` runtime |
| `UseWP8ForcedPortrait` | `0x0067a118` (.data) | = 1, hanya efektif di WP8 build |

Width/height disimpan di runtime (`.bss`), tidak bisa di-patch statis.
Solusi: hook kedua fungsi dan swap return value.

## Cara kerja

```
hook OS_ScreenGetWidth()  → return height asli
hook OS_ScreenGetHeight() → return width asli
```

Engine render dengan dimensi tertukar → hasil portrait.

## Struktur

```
portraitmod/
├── mod/
│   └── main.cpp               ← entry point + hook logic
├── include/AML/
│   └── IAMI.h
├── jni/
│   ├── Android.mk
│   └── Application.mk
├── .github/workflows/
│   └── build.yml              ← CI/CD GitHub Actions
└── portraitmod_toggle.lua     ← toggle via /portrait command
```

## Build

### Via GitHub Actions (recommended)
Push ke repo → Actions otomatis build → download artifact `libportraitmod.so`

### Via Termux lokal
```bash
export NDK=$HOME/ndk
$NDK/ndk-build \
  NDK_PROJECT_PATH=. \
  NDK_APPLICATION_MK=jni/Application.mk \
  APP_BUILD_SCRIPT=jni/Android.mk \
  NDK_OUT=obj \
  NDK_LIBS_OUT=libs \
  -j4
```

Output: `libs/armeabi-v7a/libportraitmod.so`

## Install

```bash
cp libs/armeabi-v7a/libportraitmod.so /storage/emulated/0/mods/
cp portraitmod_toggle.lua /storage/emulated/0/MoNetLoader/scripts/
```

## Usage

- Portrait mode aktif otomatis saat game load
- Ketik `/portrait` di chat untuk toggle on/off

## Debug

```bash
# Pantau log realtime
tail -f /storage/emulated/0/portraitmod_log.txt

# Filter error
grep -i "ERROR\|FAIL" /storage/emulated/0/portraitmod_log.txt
```

## Catatan

- ABI: `armeabi-v7a` (ARM32) — sesuai libGTASA.so yang dianalisa
- Thumb offset: +1 otomatis ditambahkan saat hook
- Base address diambil dari `/proc/self/maps` bukan dari `dlopen` handle

## Author

brruham | SA-MP MPDE Android Modding
