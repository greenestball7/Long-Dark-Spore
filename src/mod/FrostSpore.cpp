// FrostSpore.dll: The Long Dark's survival rules, weather and sound in Spore's cell and creature stages.
// Loaded by the Spore ModAPI injector (see src/launcher). Every Spore touch point is a row in
// design/sheets/hooks.json and is tagged HOOK:<id> here; the preflight checks each one exists.
#include <windows.h>
#include <Spore/BasicIncludes.h>
#include <Spore/Simulator/Cell/CellFunctions.h>
#include <Spore/Simulator/Cell/cCellGame.h>
#include <Spore/Simulator/Cell/cCellSerialiazibleData.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <Spore/Simulator/SubSystem/PlanetModel.h>
#include <Spore/Simulator/SubSystem/SpacePlayerData.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Simulator/cNest.h>
#include <Spore/Simulator/cPlanetRecord.h>
#include <Spore/Graphics/RenderUtils.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "SurvivalSim.h"
#include "Audio.h"
#include "Overlay.h"
#include "Log.h"

namespace {

Survival::Sim gSim(GetTickCount());
uint32_t gLastStage = 0;
ULONGLONG gLastTick = 0;
float gHudClock = 0, gDamageClock = 0, gHowlTimer = 90, gMusicTimer = 20;
float gPendingLossPercent = 0;
// cell-stage memory
Math::Vector3 gLastCellPos;
bool gHaveCellPos = false;
int gLastFood = -1;
// creature-stage memory
float gLastHunger = -1;
bool gPlanetFrozen = false;
uint32_t gRng = 0x1234567u;

float Rand01() { gRng ^= gRng << 13; gRng ^= gRng >> 17; gRng ^= gRng << 5; return (gRng % 10000) / 10000.f; }
float Dist(const Math::Vector3& a, const Math::Vector3& b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return sqrtf(x * x + y * y + z * z); }

uint32_t CurrentStage() {
	if (Simulator::IsCellGame()) return Sheets::kStageCell;          // HOOK:stage_cell
	if (Simulator::IsCreatureGame()) return Sheets::kStageCreature;  // HOOK:stage_creature
	return 0;
}

void LoadTldAudio() {
	wchar_t cache[MAX_PATH * 2] = {};
	if (!GetEnvironmentVariableW(L"FROSTSPORE_CACHE", cache, MAX_PATH * 2)) { Log::Line("no FROSTSPORE_CACHE: start Spore with the FrostSpore launcher"); return; }
	if (!Audio::Start()) return;
	for (int i = 0; i < Sheets::Count(Sheets::kContent); ++i) {
		std::wstring wav = std::wstring(cache) + L"\\" + std::wstring(Sheets::kContent[i].id, Sheets::kContent[i].id + strlen(Sheets::kContent[i].id)) + L".wav";
		bool ok = Audio::Load(i, wav);
		Log::Line("audio: %s %s", Sheets::kContent[i].id, ok ? "loaded from The Long Dark" : "not available");
	}
}

// Condition loss -> Spore health. HOOK:cell_health HOOK:creature_health
void ApplyCondition(uint32_t stage, float percent) {
	gPendingLossPercent += percent;
	if (gPendingLossPercent <= 0) return;
	if (stage == Sheets::kStageCell) {
		auto* cell = Simulator::Cell::GetPlayerCell();
		if (!cell) return;
		const float perHp = 100.f / Sheets::k_cell_max_health;
		while (gPendingLossPercent >= perHp && cell->mHealthPoints > 0) {
			cell->mHealthPoints -= 1;
			gPendingLossPercent -= perHp;
			Log::Line("cold took 1 cell HP, now %d", cell->mHealthPoints);
		}
	}
	else if (stage == Sheets::kStageCreature) {
		auto* avatar = GameNounManager.GetAvatar();
		if (!avatar) return;
		Simulator::cCombatant* c = avatar;
		float dmg = c->mMaxHealthPoints * gPendingLossPercent / 100.f;
		if (dmg <= 0) return;
		c->TakeDamage(dmg, 0, 0, avatar->GetPosition(), nullptr);
		gPendingLossPercent = 0;
	}
}

void Update() {  // HOOK:frame_tick
	ULONGLONG now = GetTickCount64();
	float dt = gLastTick ? (now - gLastTick) / 1000.f : 0;
	gLastTick = now;
	if (dt > 0.25f) dt = 0.25f;

	Overlay::Install(Graphics::RenderUtils::GetDevice());  // HOOK:hud_root
	const uint32_t stage = CurrentStage();
	if (stage != gLastStage) {
		Log::Line("stage %u -> %u", gLastStage, stage);
		gHaveCellPos = false; gLastFood = -1; gLastHunger = -1; gPlanetFrozen = false; gPendingLossPercent = 0;
		gLastStage = stage;
	}
	Overlay::State hud;
	if (!stage || GameTimeManager.IsPaused()) {
		Audio::SetMaster(stage ? 0.35f : 0.f);
		hud.visible = stage != 0;
		if (!stage) { Overlay::Publish(hud); return; }
		dt = 0;
	}
	else Audio::SetMaster(1.f);

	Survival::Inputs in;
	in.stage = stage;
	in.realSeconds = dt;
	if (stage == Sheets::kStageCell) {
		auto* cell = Simulator::Cell::GetPlayerCell();  // HOOK:cell_player
		if (cell && dt > 0) {
			Math::Vector3 p = cell->mTransform.GetOffset();
			if (gHaveCellPos) in.playerMovingFast = Dist(p, gLastCellPos) / dt > Sheets::k_cell_fast_speed;
			gLastCellPos = p; gHaveCellPos = true;
		}
		auto& data = CellGame.mpSerializableData;  // HOOK:cell_food
		if (data) {
			int food = data->mFoodProgression + data->mPlantFoodProgression;
			if (gLastFood >= 0 && food > gLastFood) in.cellFoodEaten = food - gLastFood;
			gLastFood = food;
		}
	}
	else {
		auto* avatar = GameNounManager.GetAvatar();  // HOOK:creature_avatar
		if (avatar) {
			Math::Vector3 v = avatar->GetVelocity();  // HOOK:creature_speed
			Math::Vector3 pos = avatar->GetPosition();
			Math::Vector3 up = pos; float len = Dist(pos, Math::Vector3(0, 0, 0));
			if (len > 0) { up.x /= len; up.y /= len; up.z /= len; }
			float vertical = v.x * up.x + v.y * up.y + v.z * up.z;
			float horiz = sqrtf(std::max(0.f, v.x * v.x + v.y * v.y + v.z * v.z - vertical * vertical));
			in.playerMovingFast = horiz > Sheets::k_creature_fast_speed;
			in.creatureStill = horiz < Sheets::k_creature_still_speed;
			in.creatureInWater = PlanetModel.IsInWater(pos);  // HOOK:creature_water
			if (avatar->mHerd) {
				auto& herd = avatar->mHerd;
				if (herd->mpNest && Dist(herd->mpNest->GetPosition(), pos) < Sheets::k_nest_radius) in.creatureNearNest = true;  // HOOK:creature_nest
				for (auto& member : herd->mHerd) {  // HOOK:creature_herd
					if (member && member.get() != avatar && Dist(member->GetPosition(), pos) < Sheets::k_herd_radius) { in.creatureNearHerd = true; break; }
				}
			}
			float hunger = avatar->mHunger;  // HOOK:creature_hunger
			if (gLastHunger >= 0 && hunger > gLastHunger + 0.01f) in.creatureHungerRose = hunger - gLastHunger;
			gLastHunger = hunger;
		}
		if (!gPlanetFrozen) {  // HOOK:frozen_planet
			if (auto* planet = Simulator::GetActivePlanetRecord()) {
				Log::Line("home planet temperature score %.2f -> %.2f", planet->mTemperatureScore, (float)Sheets::k_frozen_temperature_score);
				planet->mTemperatureScore = Sheets::k_frozen_temperature_score;
			}
			gPlanetFrozen = true;
		}
	}

	const int prevWeather = gSim.Weather();
	auto out = gSim.Tick(in);
	gDamageClock += dt;
	if (gDamageClock >= Sheets::k_damage_tick_seconds) { gDamageClock = 0; ApplyCondition(stage, out.conditionLossPercent); }
	else gPendingLossPercent += out.conditionLossPercent;

	// The Long Dark's sound. HOOK:weather_signal HOOK:weather_signal_light HOOK:calories_signal
	const auto& w = Sheets::kWeather[gSim.Weather()];
	const auto& amb = Sheets::kContent[w.sound];
	Audio::SetAmbience(w.sound, amb.volume);
	if (out.weatherChanged || prevWeather != gSim.Weather()) Log::Line("weather: %s", w.label);
	if (out.startedFreezing) Audio::PlayOneShot((int)Sheets::TldContentId::tld_freezing, Sheets::kContent[(int)Sheets::TldContentId::tld_freezing].volume);
	gHowlTimer -= dt;
	if (gHowlTimer <= 0) {
		const int id = stage == Sheets::kStageCell ? (int)Sheets::TldContentId::tld_ice_crack : (int)Sheets::TldContentId::tld_wolf_howl;
		Audio::PlayOneShot(id, Sheets::kContent[id].volume);
		gHowlTimer = Sheets::k_howl_min_seconds + Rand01() * (Sheets::k_howl_max_seconds - Sheets::k_howl_min_seconds);
	}
	gMusicTimer -= dt;
	if (gMusicTimer <= 0 && !Audio::MusicPlaying()) {
		Audio::PlayMusic((int)Sheets::TldContentId::tld_music, Sheets::kContent[(int)Sheets::TldContentId::tld_music].volume);
		gMusicTimer = Sheets::k_music_gap_seconds;
	}

	gHudClock += dt;
	hud.visible = true;
	hud.stage = stage;
	for (int i = 0; i < Survival::kNeedCount; ++i) hud.needs[i] = gSim.Need(i);
	hud.feelsLike = gSim.FeelsLike();
	float warmth = gSim.Need((int)Sheets::NeedsId::warmth);
	hud.frost = warmth < 40 ? (40 - warmth) / 40.f : 0;
	hud.whiteout = w.overlayAlpha;
	hud.hypothermia = gSim.Hypothermia();
	strncpy(hud.weather, w.label, sizeof(hud.weather) - 1);
	Overlay::Publish(hud);
	if (gHudClock >= 10) {
		gHudClock = 0;
		Log::Line("needs W%.0f C%.0f T%.0f F%.0f feels %.1f weather %s", gSim.Need(0), gSim.Need(1), gSim.Need(2), gSim.Need(3), gSim.FeelsLike(), w.label);
	}
}

// Our own per-frame listener: allocated and freed inside this DLL (static CRT), so no heap crosses DLLs.
class UpdateListener : public App::IMessageListener {
public:
	int AddRef() override { return ++mRefs; }
	int Release() override { int r = --mRefs; if (r == 0) delete this; return r; }
	bool HandleMessage(uint32_t id, void*) override { if (id == App::kMsgAppUpdate) Update(); return false; }
private:
	int mRefs = 0;
};

void Initialize() {
	Log::Line("FrostSpore loaded: The Long Dark x Spore, cell and creature stages");
	LoadTldAudio();
	MessageManager.AddListener(new UpdateListener(), App::kMsgAppUpdate);
}

void Dispose() { Audio::Stop(); }

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		ModAPI::AddPostInitFunction(Initialize);
		ModAPI::AddDisposeFunction(Dispose);
	}
	return TRUE;
}
