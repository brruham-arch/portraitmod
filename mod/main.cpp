/**
 * portraitmod — Force Portrait Mode for GTA SA Android (SA-MP MPDE)
 *
 * Analisa (libGTASA.so, ARM32 Thumb, stripped):
 *
 *   OS_ScreenGetWidth  @ 0x268d3c (Thumb)
 *     ldr r0, [pc, #8]
 *     add r0, pc
 *     ldr r0, [r0]       <- ptr ke struct ScreenSize
 *     ldr r0, [r0]       <- return width  (offset +0)
 *     bx  lr
 *
 *   OS_ScreenGetHeight @ 0x268d4c (Thumb)
 *     ldr r0, [pc, #8]
 *     add r0, pc
 *     ldr r0, [r0]       <- ptr ke struct ScreenSize (sama)
 *     ldr r0, [r0, #4]   <- return height (offset +4)
 *     bx  lr
 *
 *   struct ScreenSize { int width; int height; };
 *   Pointer ke struct ada di GOT (runtime, nol di file).
 *   Width/height diisi oleh engine saat startup → tidak bisa di-patch statis.
 *
 *   NVEventGetOrientation @ 0x27116c — baca .bss 0x6d81da/e0 (runtime).
 *   UseWP8ForcedPortrait  @ 0x0067a118 (.data) = 1 sudah aktif tapi
 *   hanya untuk build WP8, diabaikan di Android.
 *
 * Strategi:
 *   Hook OS_ScreenGetWidth  → return nilai height asli
 *   Hook OS_ScreenGetHeight → return nilai width asli
 *   Efek: engine render dengan dimensi tertukar → portrait
 *
 * Catatan ARM32 / libGTASA.so:
 *   - Binary adalah ARM32 Thumb, bukan ARM64
 *   - Base address runtime ARM32 BUKAN dari dlopen handle langsung
 *   - Gunakan /proc/self/maps untuk dapat base .so yang benar
 *
 * Author : brruham
 * Version: 1.0
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <android/log.h>
#include <jni.h>
#include <stdlib.h>

// ─── Macro ───────────────────────────────────────────────────────────────────

#define LOG_TAG  "libportraitmod"
#define LOGFILE  "/storage/emulated/0/portraitmod_log.txt"
#define EXPORT   __attribute__((visibility("default")))

// Offset dari analisa Termux (nm -D libGTASA.so):
// Thumb bit sudah di-strip dari nilai nm (nilai ganjil → +1 saat hook)
#define OFFSET_SCREEN_GET_WIDTH   0x268d3cUL
#define OFFSET_SCREEN_GET_HEIGHT  0x268d4cUL
#define OFFSET_EMU_GLVIEWPORT      0x001bb7b4UL

// ─── Logger ──────────────────────────────────────────────────────────────────

static void logf_write(const char* msg) {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", msg);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

static void logff(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logf_write(buf);
}

// ─── State global ────────────────────────────────────────────────────────────

static int  g_enabled      = 0;
static int  g_orig_width   = 0;
static int  g_orig_height  = 0;

// Pointer ke fungsi original (diisi Dobby saat hook)
static int (*orig_ScreenGetWidth)(void)  = nullptr;
static int (*orig_ScreenGetHeight)(void) = nullptr;
static void (*orig_emuGlViewport)(int,int,int,int) = nullptr;

// ─── Dapatkan base address libGTASA.so dari /proc/self/maps ─────────────────

static uintptr_t get_base_address(const char* soname) {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        logff("[PORTRAIT] ERROR: gagal buka /proc/self/maps");
        return 0;
    }

    char line[512];
    uintptr_t base = 0;

    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, soname) && strstr(line, "r-xp")) {
            // Format: start-end perm offset dev inode path
            base = (uintptr_t)strtoul(line, nullptr, 16);
            logff("[PORTRAIT] base %s = 0x%lx (dari maps)", soname, (unsigned long)base);
            break;
        }
    }
    fclose(maps);

    if (base == 0) {
        logff("[PORTRAIT] WARN: r-xp tidak ditemukan, coba entri pertama");
        maps = fopen("/proc/self/maps", "r");
        if (maps) {
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, soname)) {
                    base = (uintptr_t)strtoul(line, nullptr, 16);
                    logff("[PORTRAIT] base fallback = 0x%lx", (unsigned long)base);
                    break;
                }
            }
            fclose(maps);
        }
    }

    return base;
}

// ─── Hook functions ──────────────────────────────────────────────────────────

static int hook_ScreenGetWidth(void) {
    int w = orig_ScreenGetWidth();
    int h = orig_ScreenGetHeight();

    // Simpan nilai asli sekali saja
    if (g_orig_width == 0 && g_orig_height == 0) {
        g_orig_width  = w;
        g_orig_height = h;
        logff("[PORTRAIT] Ukuran asli: %d x %d", w, h);
    }

    if (g_enabled) {
        // Return height asli sebagai width → portrait
        logff("[PORTRAIT] GetWidth() → %d (portrait swap)", h);
        return h;
    }
    return w;
}

static int hook_ScreenGetHeight(void) {
    int w = orig_ScreenGetWidth();
    int h = orig_ScreenGetHeight();

    if (g_enabled) {
        // Return width asli sebagai height → portrait
        logff("[PORTRAIT] GetHeight() → %d (portrait swap)", w);
        return w;
    }
    return h;
}

// ─── API internal ────────────────────────────────────────────────────────────

static void hook_emuGlViewport(int x, int y, int width, int height) {
    if (g_enabled && width > height) {
        logff("[PORTRAIT] glViewport swap: %dx%d -> %dx%d", width, height, height, width);
        orig_emuGlViewport(x, y, height, width);
        return;
    }
    orig_emuGlViewport(x, y, width, height);
}

static void  _enable(void)     { g_enabled = 1; logf_write("[PORTRAIT] ENABLED"); }
static void  _disable(void)    { g_enabled = 0; logf_write("[PORTRAIT] DISABLED"); }
static int   _is_enabled(void) { return g_enabled; }

// ─── Struct API (untuk Lua FFI jika diperlukan) ───────────────────────────────

struct PortraitAPI {
    void (*enable)(void);
    void (*disable)(void);
    int  (*is_enabled)(void);
};

// ─── Entry points AML ────────────────────────────────────────────────────────

extern "C" {

EXPORT PortraitAPI portraitmod_api = {
    _enable,
    _disable,
    _is_enabled
};

EXPORT void* __GetModInfo() {
    static const char* info = "portraitmod|1.0|Force Portrait Mode GTA SA Android|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf_write("========================================");
    logf_write("[PORTRAIT] OnModPreLoad v1.0");
    logf_write("[PORTRAIT] portraitmod by brruham");
    logf_write("========================================");
    g_enabled     = 0;
    g_orig_width  = 0;
    g_orig_height = 0;
}

EXPORT void OnModLoad() {
    logf_write("[PORTRAIT] OnModLoad mulai...");

    // 1. Load Dobby
    logf_write("[PORTRAIT] [1/5] Load libdobby.so...");
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) {
        logff("[PORTRAIT] ERROR: dlopen libdobby.so gagal: %s", dlerror());
        return;
    }
    logf_write("[PORTRAIT] [1/5] libdobby.so OK");

    // 2. Ambil simbol Dobby
    logf_write("[PORTRAIT] [2/5] Ambil simbol DobbyHook...");
    auto dobbyHook = (int(*)(void*, void*, void**))dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) {
        logff("[PORTRAIT] ERROR: DobbyHook tidak ditemukan: %s", dlerror());
        return;
    }
    logf_write("[PORTRAIT] [2/5] DobbyHook OK");

    // 3. Dapatkan base libGTASA.so
    logf_write("[PORTRAIT] [3/5] Cari base libGTASA.so dari /proc/self/maps...");
    uintptr_t base = get_base_address("libGTASA.so");
    if (base == 0) {
        logf_write("[PORTRAIT] ERROR: base libGTASA.so tidak ditemukan");
        return;
    }
    logff("[PORTRAIT] [3/5] base = 0x%lx", (unsigned long)base);

    // 4. Hitung alamat target (Thumb: +1)
    logf_write("[PORTRAIT] [4/5] Hitung alamat fungsi...");
    void* addr_width  = (void*)(base + OFFSET_SCREEN_GET_WIDTH  + 1);
    void* addr_height = (void*)(base + OFFSET_SCREEN_GET_HEIGHT + 1);
    logff("[PORTRAIT] OS_ScreenGetWidth  → %p", addr_width);
    logff("[PORTRAIT] OS_ScreenGetHeight → %p", addr_height);

    // 5. Pasang hook OS_ScreenGetWidth
    logf_write("[PORTRAIT] [5/5] Pasang hook OS_ScreenGetWidth...");
    int ret = dobbyHook(
        addr_width,
        (void*)hook_ScreenGetWidth,
        (void**)&orig_ScreenGetWidth
    );
    if (ret != 0) {
        logff("[PORTRAIT] ERROR: DobbyHook OS_ScreenGetWidth gagal (ret=%d)", ret);
        return;
    }
    logf_write("[PORTRAIT] Hook OS_ScreenGetWidth OK");

    // 6. Pasang hook OS_ScreenGetHeight
    logf_write("[PORTRAIT] [5/5] Pasang hook OS_ScreenGetHeight...");
    ret = dobbyHook(
        addr_height,
        (void*)hook_ScreenGetHeight,
        (void**)&orig_ScreenGetHeight
    );
    if (ret != 0) {
        logff("[PORTRAIT] ERROR: DobbyHook OS_ScreenGetHeight gagal (ret=%d)", ret);
        // Rollback hook width juga tidak bisa via Dobby API standar
        // Tapi log dulu agar bisa debug
        return;
    }
    logf_write("[PORTRAIT] Hook OS_ScreenGetHeight OK");

    // 7. Aktifkan portrait otomatis saat load
    // Hook glViewport dari libGLESv2
    logf_write("[PORTRAIT] Load libGLESv2...");
    void* hGLES = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hGLES) {
        logff("[PORTRAIT] ERROR: libGLESv2: %s", dlerror());
        return;
    }
    void* addr_viewport = dlsym(hGLES, "glViewport");
    if (!addr_viewport) {
        logff("[PORTRAIT] ERROR: glViewport sym: %s", dlerror());
        return;
    }
    logff("[PORTRAIT] glViewport → %p", addr_viewport);
    ret = dobbyHook(
        addr_viewport,
        (void*)hook_emuGlViewport,
        (void**)&orig_emuGlViewport
    );
    if (ret != 0) {
        logff("[PORTRAIT] ERROR: hook glViewport gagal (ret=%d)", ret);
        return;
    }
    logf_write("[PORTRAIT] Hook glViewport OK");

    // Force portrait via AML GetJNIEnvironment + ActivityThread
    logf_write("[PORTRAIT] Force portrait via JNI...");
    do {
        void* hAML = dlopen("libAML.so", RTLD_NOW | RTLD_NOLOAD);
        if (!hAML) { logf_write("[PORTRAIT] WARN: libAML.so tidak ditemukan"); break; }

        auto GetJNIEnvironment = (JNIEnv*(*)())
            dlsym(hAML, "_ZN3AML17GetJNIEnvironmentEv");
        if (!GetJNIEnvironment) { logf_write("[PORTRAIT] WARN: GetJNIEnvironment tidak ditemukan"); break; }

        JNIEnv* env = GetJNIEnvironment();
        if (!env) { logf_write("[PORTRAIT] WARN: JNIEnv null"); break; }
        logf_write("[PORTRAIT] JNIEnv OK");

        // Ambil Activity via ActivityThread.currentActivityThread().currentActivity()
        jclass atClass = env->FindClass("android/app/ActivityThread");
        if (!atClass) { logf_write("[PORTRAIT] WARN: ActivityThread class null"); break; }

        jmethodID getCurrent = env->GetStaticMethodID(atClass,
            "currentActivityThread", "()Landroid/app/ActivityThread;");
        jobject at = env->CallStaticObjectMethod(atClass, getCurrent);
        if (!at) { logf_write("[PORTRAIT] WARN: currentActivityThread null"); break; }

        jmethodID getActivity = env->GetMethodID(atClass,
            "getApplication", "()Landroid/app/Application;");
        jobject app = env->CallObjectMethod(at, getActivity);
        if (!app) { logf_write("[PORTRAIT] WARN: getApplication null"); break; }

        // Coba currentActivity() — tidak tersedia semua versi Android
        jmethodID currentAct = env->GetMethodID(atClass, "currentActivity",
            "()Landroid/app/Activity;");
        jobject activity = nullptr;
        if (currentAct) {
            activity = env->CallObjectMethod(at, currentAct);
            if (env->ExceptionCheck()) { env->ExceptionClear(); activity = nullptr; }
        }

        // Fallback: pakai app (Application extends Context, bukan Activity)
        // setRequestedOrientation hanya bisa dipanggil pada Activity
        jobject target = activity ? activity : app;

        jclass actClass = env->FindClass("android/app/Activity");
        jmethodID setOri = env->GetMethodID(actClass,
            "setRequestedOrientation", "(I)V");

        if (target && setOri) {
            // 1 = SCREEN_ORIENTATION_PORTRAIT
            env->CallVoidMethod(target, setOri, 1);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                logf_write("[PORTRAIT] WARN: setRequestedOrientation exception");
            } else {
                logf_write("[PORTRAIT] setRequestedOrientation(PORTRAIT) OK");
            }
        } else {
            logf_write("[PORTRAIT] WARN: target/setOri null");
        }
    } while(0);

    _enable();

    logf_write("[PORTRAIT] ==============================");
    logf_write("[PORTRAIT] OnModLoad SELESAI");
    logf_write("[PORTRAIT] Portrait mode AKTIF");
    logf_write("[PORTRAIT] ==============================");
}

} // extern "C"
