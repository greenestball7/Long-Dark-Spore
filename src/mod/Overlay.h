// Draws the survival HUD (hud.json) on Spore's frames by hooking IDirect3DDevice9::Present.
#pragma once
#include <cstdint>

struct IDirect3DDevice9;

namespace Overlay {

// What to draw this frame; written by the game update, read in Present.
struct State {
	bool visible = false;
	uint32_t stage = 0;               // Sheets stage mask
	float needs[4] = {};              // 0-100, order of Sheets::kNeeds
	float feelsLike = 0;
	float frost = 0;                  // 0-1, frost_vignette strength
	float whiteout = 0;               // 0-1, whiteout strength
	bool hypothermia = false;
	char weather[24] = {};
};

bool Install(IDirect3DDevice9* device);  // patches Present once; safe to call every frame
void Publish(const State& s);

}  // namespace Overlay
