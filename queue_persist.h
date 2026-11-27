#ifndef QUEUE_PERSIST_H
#define QUEUE_PERSIST_H

#define QUEUE_STATE_FILE "game:\\queue_state.txt"
#define SETTINGS_INI_FILE "game:\\settings.ini"

struct AppFolders {
    std::string gamesDir;
    std::string xgamesDir;
    std::string isosDir;
};

static void LoadAppFolders(AppFolders &cfg) {
    cfg.gamesDir = "GAMES";
    cfg.xgamesDir = "XGAMES";
    cfg.isosDir = "ISOS";
    FILE *f = fopen(SETTINGS_INI_FILE, "r");
    if (!f) {
        f = fopen(SETTINGS_INI_FILE, "w");
        if (f) { fprintf(f, "GAMES_DIR=GAMES\nXGAMES_DIR=XGAMES\nISOS_DIR=ISOS\n"); fclose(f); }
        return;
    }
    char line[256];
    while (fgets(line, 1024, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "GAMES_DIR=", 10) == 0 && line[10]) cfg.gamesDir = line + 10;
        else if (strncmp(line, "XGAMES_DIR=", 11) == 0 && line[11]) cfg.xgamesDir = line + 11;
        else if (strncmp(line, "ISOS_DIR=", 9) == 0 && line[9]) cfg.isosDir = line + 9;
    }
    fclose(f);
}

static void SaveAppFolders(const AppFolders &cfg) {
    FILE *f = fopen(SETTINGS_INI_FILE, "w");
    if (f) { fprintf(f, "GAMES_DIR=%s\nXGAMES_DIR=%s\nISOS_DIR=%s\n", cfg.gamesDir.c_str(), cfg.xgamesDir.c_str(), cfg.isosDir.c_str()); fclose(f); }
}

static void WriteQueueStateFile(const std::vector<QueueItem> &queue, const std::vector<bool> &completed, const char *destDrive, int outMode) {
    FILE *f = fopen(QUEUE_STATE_FILE, "w");
    if (!f) return;
    fprintf(f, "%d|%s\n", outMode, destDrive);
    for (size_t i = 0; i < queue.size(); ++i)
        fprintf(f, "%d|%d|%s|%s\n", completed[i] ? 1 : 0, queue[i].deleteAfter ? 1 : 0, queue[i].sourcePath.c_str(), queue[i].displayName.c_str());
    fclose(f);
}

static bool LoadSavedQueue(std::vector<QueueItem> &outQueue, std::vector<bool> &outCompleted, std::string &outDestDrive, int &outMode) {
    FILE *f = fopen(QUEUE_STATE_FILE, "r");
    if (!f) return false;
    char line[1024];
    if (!fgets(line, 1024, f)) { fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0';
    char *sep = strchr(line, '|');
    if (sep) { *sep++ = '\0'; outMode = atoi(line); outDestDrive = sep; }
    else { outMode = 0; outDestDrive = "Hdd1:\\"; }
    bool hasPending = false;
    while (fgets(line, 1024, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p1 = strchr(line, '|'); if (!p1) continue; *p1++ = '\0';
        char *p2 = strchr(p1, '|'); if (!p2) continue; *p2++ = '\0';
        char *p3 = strchr(p2, '|'); if (!p3) continue; *p3++ = '\0';
        bool done = atoi(line) == 1;
        QueueItem qi = { p2, p3, atoi(p1) == 1 };
        outQueue.push_back(qi); outCompleted.push_back(done);
        if (!done) hasPending = true;
    }
    fclose(f);
    if (!hasPending) { DeleteFileA(QUEUE_STATE_FILE); return false; }
    return true;
}

static void ExecuteConversionBatch(const std::vector<QueueItem> &queue, std::vector<bool> &completed, const char *destDrive, int outMode, const AppFolders &cfg) {
    WriteQueueStateFile(queue, completed, destDrive, outMode);
    for (size_t q = 0; q < queue.size(); ++q) {
        if (completed[q]) continue;
        ClearScreen();
        LogPrintf("[%u/%u] Converting %s...\n\n", (unsigned int)(q + 1), (unsigned int)queue.size(), queue[q].displayName.c_str());
        int rc = (outMode == 4) ? PatchGodStorageIcon(queue[q].sourcePath.c_str(), LogPrintf) : ConvertUniversal(queue[q].sourcePath.c_str(), destDrive, outMode, cfg.gamesDir.c_str(), cfg.xgamesDir.c_str(), cfg.isosDir.c_str(), queue[q].displayName.c_str(), LogPrintf);
        if (rc == 0 || rc == 2) {
            if (rc == 0 && outMode != 4 && queue[q].deleteAfter) DeletePathRecursive(queue[q].sourcePath.c_str());
            completed[q] = true;
            WriteQueueStateFile(queue, completed, destDrive, outMode);
        } else {
            LogPrintf("\nError on item %u/%u (%s). Press A to continue...\n", (unsigned int)(q + 1), (unsigned int)queue.size(), queue[q].displayName.c_str());
            WaitButtonRelease(); XINPUT_STATE st;
            while (!(XInputGetState(0, &st) == ERROR_SUCCESS && (st.Gamepad.wButtons & XINPUT_GAMEPAD_A))) Sleep(50);
            WaitButtonRelease();
        }
    }
    DeleteFileA(QUEUE_STATE_FILE);
}
#endif




