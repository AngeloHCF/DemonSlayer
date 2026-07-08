// ---------------------------------------------------------------
// Demon Slayer — Night of the Demon King
// 2D side-view action game built with C++ and raylib only.
//
// The world renders at a fixed 1280x720 into a texture, then is
// scaled (letterboxed) to whatever the window/screen size is —
// F11 toggles fullscreen at any resolution.
// ---------------------------------------------------------------
#include "raylib.h"
#include "game.h"
#include "config.h"
#include "audio.h"
#include <cstring>

int main(int argc, char** argv) {
    // jump flags: 0 waves, 1 Akaza, 2 Muzan, 3 Douma, 4 Kokushibo
    int jump = -1;
    bool unlockAll = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0)  jump = 0;   // skip title screen
        if (strcmp(argv[i], "--akaza") == 0) jump = 1;
        if (strcmp(argv[i], "--boss") == 0)  jump = 2;   // straight to Muzan
        if (strcmp(argv[i], "--douma") == 0) jump = 3;
        if (strcmp(argv[i], "--koku") == 0)  jump = 4;
        if (strcmp(argv[i], "--unlock-all") == 0) unlockAll = true;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(cfg::SCREEN_W, cfg::SCREEN_H, "Demon Slayer — Night of the Demon King");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);        // ESC is used by menus; quit from title or window X
    AudioInit();

    RenderTexture2D target = LoadRenderTexture(cfg::SCREEN_W, cfg::SCREEN_H);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    Game game;
    game.Init();
    if (jump >= 0) game.DebugStart(jump);
    if (unlockAll) {
        if (jump < 0) game.DebugStart(0);
        game.UnlockAllForTesting();
    }

    while (!WindowShouldClose() && !game.quit) {
        if (IsKeyPressed(KEY_F11) ||
            (IsKeyDown(KEY_LEFT_ALT) && IsKeyPressed(KEY_ENTER)))
            ToggleBorderlessWindowed();

        AudioUpdate();
        game.Update(GetFrameTime());

        // draw the world at fixed resolution
        BeginTextureMode(target);
        game.Draw();
        EndTextureMode();

        // scale to the actual window (letterboxed)
        BeginDrawing();
        ClearBackground(BLACK);
        float sw = (float)GetScreenWidth();
        float sh = (float)GetScreenHeight();
        float scale = fminf(sw / cfg::SCREEN_W, sh / cfg::SCREEN_H);
        Rectangle src = { 0, 0, (float)cfg::SCREEN_W, -(float)cfg::SCREEN_H };
        Rectangle dst = {
            (sw - cfg::SCREEN_W * scale) * 0.5f,
            (sh - cfg::SCREEN_H * scale) * 0.5f,
            cfg::SCREEN_W * scale,
            cfg::SCREEN_H * scale
        };
        DrawTexturePro(target.texture, src, dst, { 0, 0 }, 0, WHITE);
        EndDrawing();
    }

    UnloadRenderTexture(target);
    AudioShutdown();
    CloseWindow();
    return 0;
}
