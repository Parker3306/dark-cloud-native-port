#pragma once
// Co-op: every player's character wears that player's colour (like the turtles' masks). The game's textures are paletted atlases of skin, hair and cloth, so the
// colour is applied by turning the hue of the strongly coloured pixels (clothes, bandanas, cloaks) and leaving skin, hair, metal and faces alone.
#include "dccore/tim2.h"

namespace dccore {

// The hue of player `index` (0 red, 1 blue, 2 green, 3 yellow, then purple, orange, cyan, pink) in degrees.
float player_hue(int index);

// Turns the hue of every strongly coloured pixel of `im` to `hue_deg` (saturation and brightness stay).
void recolor_clothes(Image& im, float hue_deg);

// Whole cloth textures of pale clothes (Xiao's white dress): the light, weakly coloured pixels get the colour as a wash.
bool is_wash_texture(const std::string& name);
void wash_cloth(Image& im, float hue_deg);

// Which textures of a character pack hold faces (never recoloured): the names end with "01" ("c01d01", "c01d01dan") or are tiny.
bool is_face_texture(const std::string& name, const Image& im);

}  // namespace dccore
