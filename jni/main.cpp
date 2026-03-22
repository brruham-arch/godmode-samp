#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>

#define TAG "GodMode"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// ── Offset di libGTASA.so ─────────────────────────────────────
#define OFF_PLAYER_PED   0x9670B4  // CWorld::Players[0].m_pPed pointer
#define OFF_HEALTH       0x540     // CPed::m_fHealth
#define OFF_ARMOR        0x548     // CPed::m_fArmour
#define MAX_HEALTH       100.0f

static uintptr_t g_gtasaBase = 0;
static bool      g_running   = true;

static void writeLog(const char* msg) {
    FILE* f = fopen("/sdcard/godmode.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
    LOGI("%s", msg);
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

// ── God Mode Thread ───────────────────────────────────────────
static void* godThread(void*) {
    writeLog("[Thread] GodMode thread started");

    while (g_running) {
        usleep(100000); // 100ms

        if (!g_gtasaBase) continue;

        // Baca pointer CPed dari CWorld::Players[0]
        uintptr_t* pedPtrAddr = (uintptr_t*)(g_gtasaBase + OFF_PLAYER_PED);
        if (!pedPtrAddr) continue;

        uintptr_t ped = *pedPtrAddr;
        if (!ped || ped < 0x1000) continue; // pointer tidak valid

        // Baca health saat ini
        float* healthPtr = (float*)(ped + OFF_HEALTH);
        float* armorPtr  = (float*)(ped + OFF_ARMOR);

        // Validasi pointer
        if ((uintptr_t)healthPtr < 0x1000) continue;

        float hp = *healthPtr;

        // Force health ke 100 kalau di bawah threshold
        if (hp > 0.0f && hp < MAX_HEALTH) {
            *healthPtr = MAX_HEALTH;
        }

        // Force armor ke 100
        float armor = *armorPtr;
        if (armor < MAX_HEALTH) {
            *armorPtr = MAX_HEALTH;
        }
    }

    return nullptr;
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

    if (!g_gtasaBase) {
        writeLog("[Init] libGTASA.so NOT FOUND!");
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "[Init] libGTASA base = 0x%x", (unsigned)g_gtasaBase);
    writeLog(buf);
    snprintf(buf, sizeof(buf), "[Init] PlayerPed ptr @ 0x%x",
        (unsigned)(g_gtasaBase + OFF_PLAYER_PED));
    writeLog(buf);

    // Start god mode thread
    pthread_t tid;
    if (pthread_create(&tid, nullptr, godThread, nullptr) == 0) {
        pthread_detach(tid);
        writeLog("[Init] GodMode thread OK — health + armor selalu penuh!");
    } else {
        writeLog("[Init] Thread FAILED!");
    }
}
