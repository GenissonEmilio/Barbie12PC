#pragma once

struct Texture;

namespace desktop_video
{
void draw(Texture texture, int width, int height, bool flip, int mode);
}
