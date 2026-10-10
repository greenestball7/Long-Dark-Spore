#include "Overlay.h"
#include "Log.h"
#include "Sheets.gen.h"
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Overlay {
namespace {

typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
Present_t gOriginalPresent = nullptr;
IDirect3DDevice9* gDevice = nullptr;
CRITICAL_SECTION gLock;
State gState;

struct Vtx { float x, y, z, rhw; DWORD color; };
std::vector<Vtx> gTris;

DWORD Argb(const uint8_t c[4], float alphaScale = 1) {
	int a = (int)(c[3] * alphaScale); a = a < 0 ? 0 : (a > 255 ? 255 : a);
	return D3DCOLOR_ARGB(a, c[0], c[1], c[2]);
}
void Quad(float x0, float y0, float x1, float y1, DWORD c00, DWORD c10, DWORD c01, DWORD c11) {
	Vtx a{x0, y0, 0, 1, c00}, b{x1, y0, 0, 1, c10}, c{x0, y1, 0, 1, c01}, d{x1, y1, 0, 1, c11};
	gTris.insert(gTris.end(), {a, b, c, b, d, c});
}
void Rect(float x0, float y0, float x1, float y1, DWORD c) { Quad(x0, y0, x1, y1, c, c, c, c); }

// 5x7 uppercase font, one string per glyph row, '#' = lit. Covers what the HUD prints.
struct Glyph { char ch; const char* rows[7]; };
const Glyph kFont[] = {
	{'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}}, {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
	{'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}}, {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
	{'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}}, {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
	{'G', {" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"}}, {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
	{'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}}, {'J', {"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}},
	{'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}}, {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
	{'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}}, {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}},
	{'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}}, {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
	{'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}}, {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
	{'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}}, {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
	{'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}}, {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
	{'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}}, {'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
	{'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}}, {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
	{'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}}, {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
	{'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}}, {'3', {"#### ", "    #", "    #", " ### ", "    #", "    #", "#### "}},
	{'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}}, {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
	{'6', {" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "}}, {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
	{'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}}, {'9', {" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "}},
	{'-', {"     ", "     ", "     ", " ### ", "     ", "     ", "     "}}, {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
	{'%', {"##  #", "##  #", "   # ", "  #  ", " #   ", "#  ##", "#  ##"}}, {':', {"     ", "  #  ", "  #  ", "     ", "  #  ", "  #  ", "     "}},
	{'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}}, {'*', {" ##  ", "#  # ", "#  # ", " ##  ", "     ", "     ", "     "}},
};
float Text(float x, float y, float px, const char* s, DWORD color) {
	for (; *s; ++s) {
		char ch = (char)toupper((unsigned char)*s);
		for (const auto& g : kFont) {
			if (g.ch != ch) continue;
			for (int r = 0; r < 7; ++r)
				for (int c = 0; c < 5; ++c)
					if (g.rows[r][c] == '#') Rect(x + c * px, y + r * px, x + (c + 1) * px, y + (r + 1) * px, color);
		}
		x += 6 * px;
	}
	return x;
}

void Build(const State& s, float W, float H) {
	gTris.clear();
	using namespace Sheets;
	auto stageOk = [&](const HudElem& e) { return (e.stages & s.stage) != 0; };
	// Overlays first (full screen). HUD:ice_tint HUD:whiteout HUD:frost_vignette
	for (const auto& e : kHud) {
		if (!stageOk(e) || e.kind != kHudOverlay) continue;
		if (!strcmp(e.binds, "stage_cell")) Rect(0, 0, W, H, Argb(e.color));
		else if (!strcmp(e.binds, "weather") && s.whiteout > 0) {
			uint8_t c[4] = {e.color[0], e.color[1], e.color[2], 255};
			Rect(0, 0, W, H, Argb(c, s.whiteout));
		}
		else if (!strcmp(e.binds, "warmth_low") && s.frost > 0) {
			// The Long Dark's freezing frost creeping in from the screen edges
			uint8_t c[4] = {e.color[0], e.color[1], e.color[2], 255};
			DWORD edge = Argb(c, 0.85f * s.frost), clear = Argb(c, 0);
			float d = (0.12f + 0.25f * s.frost) * (W < H ? W : H);
			Quad(0, 0, W, d, edge, edge, clear, clear);
			Quad(0, H - d, W, H, clear, clear, edge, edge);
			Quad(0, 0, d, H, edge, clear, edge, clear);
			Quad(W - d, 0, W, H, clear, edge, clear, edge);
		}
	}
	// Panel, bars, text, anchored bottom-left (hud.json y is measured from the bottom). HUD:hud_panel HUD:bar_warmth HUD:bar_calories HUD:bar_thirst HUD:bar_fatigue HUD:txt_feels
	float scale = H / 1080.f; if (scale < 0.75f) scale = 0.75f;
	auto X = [&](float v) { return v * scale; };
	auto Y = [&](float v, float h) { return H - (v + h) * scale; };
	for (const auto& e : kHud) {
		if (!stageOk(e)) continue;
		float x0 = X(e.x), y0 = Y(e.y, e.h), x1 = X(e.x + e.w), y1 = Y(e.y, 0);
		if (e.kind == kHudPanel) Rect(x0, y0, x1, y1, Argb(e.color));
		else if (e.kind == kHudBar) {
			int n = -1;
			for (int i = 0; i < Count(kNeeds); ++i) if (!strcmp(kNeeds[i].id, e.binds)) n = i;
			if (n < 0) continue;
			float v = s.needs[n] / 100.f;
			uint8_t back[4] = {40, 46, 54, 200};
			float labelW = 6 * 2 * scale * 9;
			Text(x0, y0 + (e.h * scale - 7 * 2 * scale) / 2, 2 * scale, kNeeds[n].label, D3DCOLOR_ARGB(230, 230, 236, 242));
			float bx = x0 + labelW;
			Rect(bx, y0, x1, y1, Argb(back));
			bool low = v < 0.2f;
			Rect(bx, y0, bx + (x1 - bx) * v, y1, low ? D3DCOLOR_ARGB(240, 220, 60, 50) : Argb(e.color));
		}
		else if (e.kind == kHudText) {
			char buf[64];
			int t = (int)(s.feelsLike < 0 ? s.feelsLike - 0.5f : s.feelsLike + 0.5f);
			snprintf(buf, sizeof buf, "FEELS %+d*C  %s", t, s.weather);
			Text(x0, y0, 2.4f * scale, buf, Argb(e.color));
			if (s.hypothermia) Text(x0, y0 - 22 * scale, 2.4f * scale, "HYPOTHERMIA", D3DCOLOR_ARGB(255, 230, 70, 60));
		}
	}
}

void Draw(IDirect3DDevice9* dev) {
	State s;
	EnterCriticalSection(&gLock);
	s = gState;
	LeaveCriticalSection(&gLock);
	if (!s.visible) return;

	IDirect3DSurface9* back = nullptr;
	if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
	D3DSURFACE_DESC desc; back->GetDesc(&desc);
	Build(s, (float)desc.Width, (float)desc.Height);
	if (gTris.empty()) { back->Release(); return; }

	IDirect3DStateBlock9* saved = nullptr;
	if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved))) { back->Release(); return; }
	IDirect3DSurface9* oldRt = nullptr;
	dev->GetRenderTarget(0, &oldRt);
	D3DVIEWPORT9 oldVp; dev->GetViewport(&oldVp);
	dev->SetRenderTarget(0, back);
	D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0, 1};
	dev->SetViewport(&vp);
	if (SUCCEEDED(dev->BeginScene())) {
		dev->SetVertexShader(nullptr); dev->SetPixelShader(nullptr);
		dev->SetTexture(0, nullptr);
		dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); dev->SetRenderState(D3DRS_LIGHTING, FALSE);
		dev->SetRenderState(D3DRS_FOGENABLE, FALSE); dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF); dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(gTris.size() / 3), gTris.data(), sizeof(Vtx));
		dev->EndScene();
	}
	saved->Apply();
	saved->Release();
	if (oldRt) { dev->SetRenderTarget(0, oldRt); oldRt->Release(); }
	dev->SetViewport(&oldVp);
	back->Release();
}

HRESULT WINAPI HookedPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r) {
	Draw(dev);
	return gOriginalPresent(dev, a, b, w, r);
}

}  // namespace

bool Install(IDirect3DDevice9* device) {
	if (!device) return false;
	if (gDevice == device) return true;
	static bool lockReady = false;
	if (!lockReady) { InitializeCriticalSection(&gLock); lockReady = true; }
	void** vtbl = *(void***)device;
	const int kPresent = 17;  // IDirect3DDevice9::Present
	if (vtbl[kPresent] == (void*)&HookedPresent) { gDevice = device; return true; }
	DWORD old;
	if (!VirtualProtect(&vtbl[kPresent], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
	gOriginalPresent = (Present_t)vtbl[kPresent];
	vtbl[kPresent] = (void*)&HookedPresent;
	VirtualProtect(&vtbl[kPresent], sizeof(void*), old, &old);
	gDevice = device;
	Log::Line("overlay: Present hooked on device %p", device);
	return true;
}

void Publish(const State& s) {
	if (!gDevice) return;  // the lock exists once Install succeeded
	EnterCriticalSection(&gLock);
	gState = s;
	LeaveCriticalSection(&gLock);
}

}  // namespace Overlay
