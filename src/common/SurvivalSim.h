// The Long Dark's survival rules, driven entirely by the design sheets (Sheets.gen.h).
// Pure C++ with no Spore types, so it can be unit-tested off-game (tests/sim_test.cpp).
#pragma once
#include "Sheets.gen.h"
#include <cstdint>
#include <cstring>

namespace Survival {

constexpr int kNeedCount = static_cast<int>(Sheets::NeedsId::Count);
constexpr int kWeatherCount = static_cast<int>(Sheets::WeatherId::Count);

// What Spore told us this frame. Signal names match hooks.json "signal" values.
struct Inputs {
	float realSeconds = 0;          // real time since last tick (already clamped by caller)
	uint32_t stage = 0;             // Sheets::kStageCell / kStageCreature, 0 = not a covered stage
	bool playerMovingFast = false;  // player_moving_fast
	bool creatureStill = false;     // creature_still
	bool creatureInWater = false;   // creature_in_water
	bool creatureNearNest = false;  // creature_near_nest
	bool creatureNearHerd = false;  // creature_near_herd
	int cellFoodEaten = 0;          // cell_food_eaten: food items eaten since last tick
	float creatureHungerRose = 0;   // creature_hunger_rose: Spore hunger points gained since last tick
};

// What the game should do as a result.
struct Outputs {
	float conditionLossPercent = 0;  // percent of max health to remove this tick
	bool startedFreezing = false;    // warmth just hit the freezing threshold (play a shiver)
	bool weatherChanged = false;
};

class Sim {
public:
	explicit Sim(uint32_t seed = 0x5F0E3D1u) { Reset(seed); }

	void Reset(uint32_t seed) {
		mRng = seed ? seed : 1u;
		for (int i = 0; i < kNeedCount; ++i) mNeeds[i] = Sheets::kNeeds[i].start;
		mWeather = static_cast<int>(Sheets::WeatherId::weather_clear);
		mWeatherHoursLeft = RandRange(Sheets::kWeather[mWeather].minHours, Sheets::kWeather[mWeather].maxHours);
		mHoursAtZeroWarmth = 0;
		mFeelsLike = 0;
		mGameHours = 0;
	}

	Outputs Tick(const Inputs& in) {
		Outputs out;
		if (!in.stage || in.realSeconds <= 0) return out;
		const float hours = in.realSeconds / Sheets::k_real_seconds_per_hour;
		mGameHours += hours;

		// Weather clock (weather.json)
		mWeatherHoursLeft -= hours;
		if (mWeatherHoursLeft <= 0) {
			const auto& w = Sheets::kWeather[mWeather];
			int options = 0;
			while (options < 4 && w.next[options] >= 0) ++options;
			mWeather = w.next[options > 1 ? Rand() % options : 0];
			const auto& nw = Sheets::kWeather[mWeather];
			mWeatherHoursLeft = RandRange(nw.minHours, nw.maxHours);
			out.weatherChanged = true;
		}

		// Feels-like temperature (temperature.json)
		mFeelsLike = 0;
		for (const auto& t : Sheets::kTempTerms) {
			if ((t.stages & in.stage) && Signal(t.appliesWhen, in)) mFeelsLike += t.celsius;
		}

		// Warmth follows the feels-like temperature
		const int W = static_cast<int>(Sheets::NeedsId::warmth);
		const bool wasAboveFreezing = mNeeds[W] > kFreezingThreshold;
		if (mFeelsLike < 0) mNeeds[W] -= -mFeelsLike * Sheets::kWarmthDrainPerDegree * hours;
		else mNeeds[W] += mFeelsLike * Sheets::kWarmthGainPerDegree * hours;

		// Other needs drain at the idle or active rate (needs.json)
		for (int i = 0; i < kNeedCount; ++i) {
			const auto& n = Sheets::kNeeds[i];
			if (!(n.stages & in.stage)) continue;
			mNeeds[i] -= (in.playerMovingFast ? n.drainActive : n.drainIdle) * hours;
		}

		// Restores (restore.json)
		for (const auto& r : Sheets::kRestores) {
			if (!(r.stages & in.stage)) continue;
			if (r.continuous) {
				if (Signal(r.trigger, in)) mNeeds[r.need] += r.amount * hours;
			}
			else if (!strcmp(r.trigger, "cell_food_eaten")) mNeeds[r.need] += r.amount * in.cellFoodEaten;
			else if (!strcmp(r.trigger, "creature_hunger_rose")) mNeeds[r.need] += r.amount * in.creatureHungerRose;
		}

		for (float& v : mNeeds) v = v < 0 ? 0 : (v > 100 ? 100 : v);
		out.startedFreezing = wasAboveFreezing && mNeeds[W] <= kFreezingThreshold;

		// Condition loss for every empty need (needs.json), hypothermia doubles freezing (temperature.json rates)
		if (mNeeds[W] <= 0) mHoursAtZeroWarmth += hours; else mHoursAtZeroWarmth = 0;
		for (int i = 0; i < kNeedCount; ++i) {
			const auto& n = Sheets::kNeeds[i];
			if (!(n.stages & in.stage) || mNeeds[i] > 0) continue;
			float loss = n.conditionLossWhenEmpty;
			if (i == W && mHoursAtZeroWarmth >= Sheets::kHypothermiaAfterHours) loss *= Sheets::kHypothermiaMultiplier;
			out.conditionLossPercent += loss * hours;
		}
		return out;
	}

	float Need(int i) const { return mNeeds[i]; }
	void SetNeed(int i, float v) { mNeeds[i] = v; }
	float FeelsLike() const { return mFeelsLike; }
	int Weather() const { return mWeather; }
	void ForceWeather(int w) { mWeather = w; mWeatherHoursLeft = Sheets::kWeather[w].maxHours; }
	bool Hypothermia() const { return mHoursAtZeroWarmth >= Sheets::kHypothermiaAfterHours; }
	float GameHours() const { return mGameHours; }

	// The signal table shared with hooks.json; the sim derives its own internal signals.
	bool Signal(const char* s, const Inputs& in) const {
		if (!strcmp(s, "always")) return true;
		if (!strcmp(s, "player_moving_fast")) return in.playerMovingFast;
		if (!strcmp(s, "creature_still")) return in.creatureStill;
		if (!strcmp(s, "creature_in_water")) return in.creatureInWater;
		if (!strcmp(s, "creature_near_nest")) return in.creatureNearNest;
		if (!strcmp(s, "creature_near_herd")) return in.creatureNearHerd;
		if (!strcmp(s, "weather_blizzard")) return mWeather == static_cast<int>(Sheets::WeatherId::weather_blizzard);
		if (!strcmp(s, "weather_light_snow")) return mWeather == static_cast<int>(Sheets::WeatherId::weather_light_snow);
		if (!strcmp(s, "calories_above_50")) return mNeeds[static_cast<int>(Sheets::NeedsId::calories)] > 50;
		if (!strcmp(s, "stage_cell")) return in.stage == Sheets::kStageCell;
		if (!strcmp(s, "stage_creature")) return in.stage == Sheets::kStageCreature;
		return false;
	}

	static constexpr float kFreezingThreshold = 20.0f;

private:
	uint32_t Rand() { mRng ^= mRng << 13; mRng ^= mRng >> 17; mRng ^= mRng << 5; return mRng; }
	float RandRange(float a, float b) { return a + (b - a) * (Rand() % 10000) / 10000.0f; }

	float mNeeds[kNeedCount];
	int mWeather;
	float mWeatherHoursLeft;
	float mHoursAtZeroWarmth;
	float mFeelsLike;
	float mGameHours;
	uint32_t mRng;
};

}  // namespace Survival
