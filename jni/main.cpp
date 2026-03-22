#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#define TAG "GodMode"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// ── Offset di libGTASA.so ─────────────────────────────────────
// CPedDamageResponseCalculator::ComputeWillKillPed
#define OFF_WILL_KILL_PED    0x371F90  // size 424, return bool
// CPedDamageResponseCalculator::ComputeDamageResponse
#define OFF_DAMAGE_RESPONSE  0x371AEC  // size 776
// CPlayerInfo::AddHealth
#define OFF_ADD_HEALTH       0x40C296  // size 40
// PLAYER_PED global pointer di libGTASA
#define OFF_PLAYER_PED_PTR   0x9670B4

// ── Global ────────────────────────────────────────────────────
static uintptr_t g_gtasaBase = 0;
static bool      g_enabled   = true;

static void writeLog(const char* msg) {
    FILE* f = fopen("/sdcard/godmode.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
    LOGI("%s", msg);
}

// ── CPedDamageResponse struct (partial) ───────────────────────
struct CPedDamageResponse {
    bool bHealthZero;       // +0x00
    bool bKillPed;          // +0x01
    bool bMakeHeadlessPed;  // +0x02
    bool bForceDeath;       // +0x03
    float fDamage;          // +0x04 — damage yang akan diapply
    // ... fields lain
};

// ── Hook: ComputeWillKillPed ──────────────────────────────────
// bool CPedDamageResponseCalculator::ComputeWillKillPed(
//     CPed* ped, CPedDamageResponse& response, bool unused)
typedef bool (*ComputeWillKillPed_t)(void* calc, void* ped,
                                      CPedDamageResponse* resp, bool unk);
static ComputeWillKillPed_t orig_WillKillPed = nullptr;

static bool hooked_WillKillPed(void* calc, void* ped,
                                CPedDamageResponse* resp, bool unk) {
    if (!g_enabled) return orig_WillKillPed(calc, ped, resp, unk);

    // Cek apakah ini local player
    uintptr_t playerPedPtr = *(uintptr_t*)(g_gtasaBase + OFF_PLAYER_PED_PTR);
    if (ped && (uintptr_t)ped == playerPedPtr) {
        // Local player — tidak boleh mati
        if (resp) {
            resp->bKillPed    = false;
            resp->bForceDeath = false;
            resp->fDamage     = 0.0f;
        }
        return false; // tidak akan mati
    }

    return orig_WillKillPed(calc, ped, resp, unk);
}

// ── Hook: ComputeDamageResponse ───────────────────────────────
// void CPedDamageResponseCalculator::ComputeDamageResponse(
//     CPed* ped, CPedDamageResponse& response, bool unused)
typedef void (*ComputeDamageResponse_t)(void* calc, void* ped,
                                         CPedDamageResponse* resp, bool unk);
static ComputeDamageResponse_t orig_DamageResponse = nullptr;

static void hooked_DamageResponse(void* calc, void* ped,
                                   CPedDamageResponse* resp, bool unk) {
    orig_DamageResponse(calc, ped, resp, unk);

    if (!g_enabled) return;

    // Cek apakah ini local player
    uintptr_t playerPedPtr = *(uintptr_t*)(g_gtasaBase + OFF_PLAYER_PED_PTR);
    if (ped && (uintptr_t)ped == playerPedPtr) {
        // Zero out semua damage untuk local player
        if (resp) {
            resp->fDamage     = 0.0f;
            resp->bKillPed    = false;
            resp->bForceDeath = false;
            resp->bHealthZero = false;
        }
    }
}

// ── Patch helper ──────────────────────────────────────────────
static bool patchThumb(uintptr_t addr, void* hookFn, void** origFn) {
    uintptr_t raw = addr & ~1u;
    if (origFn) *origFn = (void*)(raw | 1u);
    uint32_t patch[2];
    patch[0] = 0xF000F8DF; // LDR.W PC, [PC, #0]
    patch[1] = (uint32_t)(uintptr_t)hookFn;
    uintptr_t page = raw & ~((uintptr_t)(getpagesize()-1));
    if (mprotect((void*)page, getpagesize()*2,
                 PROT_READ|PROT_WRITE|PROT_EXEC) != 0) return false;
    memcpy((void*)raw, patch, 8);
    __builtin___clear_cache((char*)raw, (char*)(raw+8));
    mprotect((void*)page, getpagesize()*2, PROT_READ|PROT_EXEC);
    return true;
}

static uintptr_t getLibBase(const char* name) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = (uintptr_t)-1;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, name)) continue;
        uintptr_t s = (uintptr_t)strtoul(line, nullptr, 16);
        if (s < base) base = s;
    }
    fclose(f);
    return (base == (uintptr_t)-1) ? 0 : base;
}

// ── Init ──────────────────────────────────────────────────────
__attribute__((constructor))
static void onLoad() {
    writeLog("=== GodMode START ===");

    // Tunggu libGTASA load
    for (int i = 0; i < 50 && !g_gtasaBase; i++) {
        g_gtasaBase = getLibBase("libGTASA.so");
        if (!g_gtasaBase) usleep(200000);
    }
    if (!g_gtasaBase) { writeLog("[Init] libGTASA.so NOT FOUND!"); return; }

    char buf[128];
    snprintf(buf, sizeof(buf), "[Init] libGTASA.so base = 0x%x",
        (unsigned)g_gtasaBase);
    writeLog(buf);

    // Hook ComputeWillKillPed
    uintptr_t addrWillKill = g_gtasaBase + OFF_WILL_KILL_PED;
    if (patchThumb(addrWillKill | 1,
                   (void*)hooked_WillKillPed,
                   (void**)&orig_WillKillPed)) {
        writeLog("[Hook] ComputeWillKillPed OK");
    } else {
        writeLog("[Hook] ComputeWillKillPed FAILED");
    }

    // Hook ComputeDamageResponse
    uintptr_t addrDmgResp = g_gtasaBase + OFF_DAMAGE_RESPONSE;
    if (patchThumb(addrDmgResp | 1,
                   (void*)hooked_DamageResponse,
                   (void**)&orig_DamageResponse)) {
        writeLog("[Hook] ComputeDamageResponse OK");
    } else {
        writeLog("[Hook] ComputeDamageResponse FAILED");
    }

    writeLog("[Init] GodMode siap! Darah selalu penuh.");
}
