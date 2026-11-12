#include "GamesMenu.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/games/games.h"

namespace {
enum MenuAction : int { ACT_NONE = -1, ACT_SOUND = 100, ACT_SWAP = 101 };
}

void GamesMenu::optionsMenu() {
    int index = 0;
    while (true) {
        // Options only record the choice; it runs after loopOptions returns, so
        // nothing replaces the shared `options` list while one of its entries
        // is still executing.
        int action = ACT_NONE;
        options.clear();
        for (int g = 0; g < GAME_COUNT; g++) {
            const uint32_t best = gamesBestScore((GameId)g);
            String label = gameName((GameId)g);
            if (best > 0) label += String("  (") + String(best) + ")";
            options.push_back(Option(label, [&action, g]() { action = g; }));
        }
        options.push_back(Option(String("Sound: ") + (gamesSoundOn() ? "ON" : "OFF"), [&action]() {
            action = ACT_SOUND;
        }));
        if (gamesHasDirectButtons()) {
            options.push_back(Option(String("Swap L/R: ") + (gamesSwapButtons() ? "ON" : "OFF"), [&action]() {
                action = ACT_SWAP;
            }));
        }
        addOptionToMainMenu();

        index = loopOptions(options, MENU_TYPE_SUBMENU, "Games", index);
        if (index < 0 || returnToMenu) break; // Esc / long-press side, or "Main Menu"

        if (action >= 0 && action < GAME_COUNT) playGame((GameId)action);
        else if (action == ACT_SOUND) gamesSetSoundOn(!gamesSoundOn());
        else if (action == ACT_SWAP) gamesSetSwapButtons(!gamesSwapButtons());
    }
}

void GamesMenu::drawIcon(float scale) {
    clearIconArea();

    const uint16_t fg = bruceConfig.priColor;
    const uint16_t bg = bruceConfig.bgColor;

    // Controller body
    const int w = scale * 74;
    const int h = scale * 40;
    const int x = iconCenterX - w / 2;
    const int y = iconCenterY - h / 2;
    tft.fillRoundRect(x, y, w, h, h / 2, fg);

    // D-pad on the left
    const int dpx = x + h / 2 + scale * 2;
    const int arm = scale * 8;
    const int thick = max(2, (int)(scale * 5));
    tft.fillRect(dpx - arm, iconCenterY - thick / 2, 2 * arm, thick, bg);
    tft.fillRect(dpx - thick / 2, iconCenterY - arm, thick, 2 * arm, bg);

    // Two buttons on the right
    const int bx = x + w - h / 2 - scale * 2;
    const int br = max(2, (int)(scale * 4));
    tft.fillCircle(bx + scale * 5, iconCenterY - scale * 4, br, bg);
    tft.fillCircle(bx - scale * 5, iconCenterY + scale * 4, br, bg);
}
