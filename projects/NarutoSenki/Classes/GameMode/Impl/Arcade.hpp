#pragma once
#include "GameMode/Impl/Duels.hpp"
#include "MyUtils/KTools.h"
#include "MyUtils/UnlockRequirements.hpp"

// Arcade: an endless chain of Duels stages.
//
// A stage is an ordinary Duels match. Winning moves the streak on, and the
// results screen offers to continue straight into the next stage with the same
// team; losing resets the streak. Every kBossInterval-th stage is a boss round:
// the enemy is one of bossPool(), has no bench and 5x HP instead of 2x. Boss
// heroes are kept out of the normal enemy pool so that fight stays a surprise.
// Clearing a boss round can unlock the hero keyed to that boss (see
// MyUtils/UnlockRequirements.hpp).
class ModeArcade : public ModeDuels
{
public:
	static constexpr int kBossInterval = 10;
	static constexpr uint32_t kBossHpMultiplier = 5;

	void init() override
	{
		ModeDuels::init();
		CCLOG("Enter Arcade mode.");

		// The saved streak is the number of stages already cleared.
		stage = KTools::readArcadeStreak() + 1;
	}

	void onGameStart() override { showStageLabel(); }

	bool usesCustomClock() const override { return true; }

	// Boss rounds fight in their own arena with their own theme:
	//     map:   Maps/Duels/<n>.tmx, n from ArenaBosses (its background is the
	//            usual Maps/Duels/map_bg<n>.png)
	//     music: Audio/OST/<Hero>.ogg
	// Either one is skipped on its own if its file is not there.
	ArenaOverrides getArenaOverrides() override
	{
		const auto &heroes = getHerosArray();
		if (!isBossRound() || heroes.size() < 2)
			return {};

		ArenaOverrides overrides;
		for (const auto &boss : ArenaBosses::all())
		{
			if (heroes[1].name == boss.hero)
				overrides.map = boss.map;
		}
		overrides.music = format("Audio/OST/{}.ogg", heroes[1].name);
		return overrides;
	}

	int currentStage() const { return stage; }
	bool isBossRound() const { return stage % kBossInterval == 0; }

	// The player's side lost everything -> the run is over and the live streak
	// resets. The per-hero best stage is a permanent record and is untouched.
	// The enemy's side lost everything -> the stage is cleared.
	void onSideEliminated(bool playerSide) override
	{
		if (playerSide)
		{
			KTools::saveArcadeStreak(0);
			return;
		}

		KTools::saveArcadeStreak(stage);
		recordStageForTeam();

		if (isBossRound())
		{
			const auto &heroes = getHerosArray();
			if (heroes.size() > 1)
				Unlocks::onArcadeBossDefeated(heroes[1].name);
		}

		// Records may have just satisfied a stage-based unlock rule.
		Unlocks::onProgressChanged();
	}

	// Leaving through the pause menu is not a loss: the streak is kept, so the
	// run can be resumed from the same stage later.
	void onSurrender() override {}

protected:
	uint32_t hpMultiplier(bool playerSide) override
	{
		return (isBossRound() && !playerSide) ? kBossHpMultiplier : ModeDuels::hpMultiplier(playerSide);
	}

	// The boss pool, from the ArenaBosses table.
	static const vector<string> &bossPool()
	{
		static const vector<string> pool = []
		{
			vector<string> names;
			for (const auto &boss : ArenaBosses::all())
				names.push_back(boss.hero);
			return names;
		}();
		return pool;
	}

	vector<string> reservedHeroes() override { return bossPool(); }
	bool enemyGetsBench() override { return !isBossRound(); }

	// initHeros() rolled the enemy (index 1; the player is always index 0 in a
	// one-versus-one) from the full pool, which knows nothing about bosses.
	void arrangeEnemy() override
	{
		const auto &pool = bossPool();
		if (isBossRound())
		{
			setRand();
			overrideHeroName(1, pool[random(static_cast<int>(pool.size()))]);
			return;
		}

		const auto &heroes = getHerosArray();
		if (heroes.size() < 2)
			return;

		const string rolled = heroes[1].name;
		if (std::find(pool.begin(), pool.end(), rolled) == pool.end())
			return;

		// Rolled a boss on an ordinary stage: re-roll without bosses, and
		// without anyone already on the player's team.
		vector<string> avoid = pool;
		avoid.push_back(heroes[0].name);
		avoid.insert(avoid.end(), playerBench.begin(), playerBench.end());
		overrideHeroName(1, getRandomHeroExceptAll(avoid));
	}

	// Remember the team so the mode menu can offer to resume this run.
	void onRostersChosen() override
	{
		KTools::ArcadeTeam team;
		team.leader = selectLayer->_playerSelect ? selectLayer->_playerSelect : "";
		team.ally1 = playerBench.size() > 0 ? playerBench[0] : "";
		team.ally2 = playerBench.size() > 1 ? playerBench[1] : "";
		KTools::saveArcadeTeam(team);
	}

private:
	int stage = 1;

	void showStageLabel()
	{
		auto layer = getGameLayer();
		if (!layer || !layer->getHudLayer() || !layer->getHudLayer()->gameClock)
			return;

		const string text = isBossRound() ? format("Stage {} - BOSS", stage) : format("Stage {}", stage);
		layer->getHudLayer()->gameClock->setString(text.c_str());
	}

	// Credits this stage to the best-stage record of every hero on the
	// player's team: the one on the field plus the two on the bench. Fallen
	// heroes keep their bench slot name, so they count too.
	void recordStageForTeam()
	{
		auto layer = getGameLayer();
		if (!layer)
			return;

		auto &duel = layer->duel();
		vector<string> team = duel.side(true).bench;
		if (Hero *active = duel.activeHero(true))
			team.push_back(duel.keyOf(active, true)); // spawn name, so transformations don't matter

		for (const auto &name : team)
		{
			if (!name.empty())
				KTools::saveArcadeBestIfHigher(name.c_str(), stage);
		}
	}
};
