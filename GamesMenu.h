#ifndef __GAMES_MENU_H__
#define __GAMES_MENU_H__

#include <MenuItemInterface.h>

class GamesMenu : public MenuItemInterface {
public:
    GamesMenu() : MenuItemInterface("Games") {}

    void optionsMenu(void) override;
    void drawIcon(float scale) override;

    // Themes don't have a Games image yet, so always use the drawn icon.
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String none;
        return none;
    }
};

#endif
