// Off-game checks of the survival rules against the sheet values.
#include "SurvivalSim.h"
#include <cstdio>
#include <cmath>
using namespace Survival;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static Inputs base(uint32_t stage, float secs) { Inputs in; in.stage = stage; in.realSeconds = secs; return in; }
int main() {
	const float H = Sheets::k_real_seconds_per_hour;
	const int W = (int)Sheets::NeedsId::warmth, C = (int)Sheets::NeedsId::calories, T = (int)Sheets::NeedsId::thirst, F = (int)Sheets::NeedsId::fatigue;
	{ // cell stage in clear weather: -4C ocean, calories 75 (>50 => +3) => feels -1C
		Sim s; s.ForceWeather((int)Sheets::WeatherId::weather_clear);
		auto o = s.Tick(base(Sheets::kStageCell, H));
		CHECK(std::fabs(s.FeelsLike() - (-1)) < 1e-4, "feels %f", s.FeelsLike());
		CHECK(std::fabs(s.Need(W) - (100 - 1 * Sheets::kWarmthDrainPerDegree)) < 1e-3, "warmth %f", s.Need(W));
		CHECK(std::fabs(s.Need(C) - 71) < 1e-3, "calories %f", s.Need(C));
		CHECK(s.Need(T) == 100 && s.Need(F) == 100, "thirst/fatigue untouched in cell");
		CHECK(o.conditionLossPercent == 0, "no loss");
	}
	{ // blizzard in creature stage, nothing else: -10 -18 +3 = -25C
		Sim s; s.ForceWeather((int)Sheets::WeatherId::weather_blizzard);
		s.Tick(base(Sheets::kStageCreature, H * 0.5f));
		CHECK(std::fabs(s.FeelsLike() - (-25)) < 1e-4, "blizzard feels %f", s.FeelsLike());
	}
	{ // nest shelter warms you up: -10 +22 +3 = +15C in clear weather
		Sim s; s.ForceWeather((int)Sheets::WeatherId::weather_clear); s.SetNeed(W, 10);
		Inputs in = base(Sheets::kStageCreature, H); in.creatureNearNest = true;
		s.Tick(in);
		CHECK(s.FeelsLike() == 15 && s.Need(W) > 10, "nest warms: feels %f warmth %f", s.FeelsLike(), s.Need(W));
	}
	{ // empty warmth costs 20%/h, hypothermia after 3h doubles it
		Sim s; s.ForceWeather((int)Sheets::WeatherId::weather_blizzard); s.SetNeed(W, 0);
		auto o = s.Tick(base(Sheets::kStageCreature, H));
		CHECK(std::fabs(o.conditionLossPercent - 20) < 1e-3, "freeze loss %f", o.conditionLossPercent);
		s.Tick(base(Sheets::kStageCreature, H)); s.Tick(base(Sheets::kStageCreature, H));
		o = s.Tick(base(Sheets::kStageCreature, H));
		CHECK(s.Hypothermia() && std::fabs(o.conditionLossPercent - 40) < 1e-3, "hypothermia loss %f", o.conditionLossPercent);
	}
	{ // eating in cell restores calories and a bit of warmth
		Sim s; s.SetNeed(C, 10); s.SetNeed(W, 50);
		Inputs in = base(Sheets::kStageCell, 0.001f); in.cellFoodEaten = 2;
		s.Tick(in);
		CHECK(s.Need(C) > 21.9f && s.Need(W) > 57.9f, "eat: cal %f warm %f", s.Need(C), s.Need(W));
	}
	{ // drinking and resting refill
		Sim s; s.SetNeed(T, 0); s.SetNeed(F, 0);
		Inputs in = base(Sheets::kStageCreature, H); in.creatureInWater = true; in.creatureStill = true;
		s.Tick(in);
		CHECK(s.Need(T) > 50 && s.Need(F) > 5, "drink %f rest %f", s.Need(T), s.Need(F));
	}
	{ // weather cycles over a long run and visits every state
		Sim s(1234); bool seen[3] = {};
		for (int i = 0; i < 4000; ++i) { s.Tick(base(Sheets::kStageCreature, H / 4)); seen[s.Weather()] = true; }
		CHECK(seen[0] && seen[1] && seen[2], "weather states visited %d %d %d", seen[0], seen[1], seen[2]);
	}
	{ // no stage -> nothing happens
		Sim s; auto before = s.Need(W); s.Tick(base(0, 100)); CHECK(s.Need(W) == before, "inactive");
	}
	printf(fails ? "%d FAILED\n" : "all sim tests passed\n", fails);
	return fails ? 1 : 0;
}
