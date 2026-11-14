#include "presentation.h"
#include "raylib.h"

#include <algorithm>

void gow2DesktopInitialize() { SetExitKey(KEY_NULL); }
void gow2DesktopShutdown() {}
bool gow2DesktopShouldExit() { return false; }
int gow2DesktopMode() { return 0; }
void gow2DesktopSnapshot() {}

void gow2DesktopKeys()
{
    if (IsKeyPressed(KEY_F11))
        ToggleBorderlessWindowed();
}

void desktop_video::draw(Texture2D texture, int width, int height, bool flip, int)
{
    const float screenWidth = GetScreenWidth();
    const float screenHeight = GetScreenHeight();
    const float fittedHeight = std::min(screenHeight, screenWidth * 0.75f);
    const float fittedWidth = fittedHeight * (4.0f / 3.0f);
    const Rectangle source{0.0f, 0.0f, static_cast<float>(width),
                           static_cast<float>(flip ? -height : height)};
    const Rectangle destination{(screenWidth - fittedWidth) * 0.5f,
                                (screenHeight - fittedHeight) * 0.5f, fittedWidth, fittedHeight};
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
}
