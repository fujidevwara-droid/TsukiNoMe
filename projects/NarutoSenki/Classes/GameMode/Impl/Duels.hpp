#pragma once
#include "GameMode/IGameModeHandler.hpp"
#include "Systems/DuelSystem.hpp"

// The Arcade bosses and the arena (Maps/Duels/<map>.tmx) each one fights in.
// It lives here rather than in Arcade.hpp because Duels needs the map numbers
// too: those maps are reserved for the bosses and must stay out of the random
// arena pick in every duel mode. Madara is left out because this build does not
// ship him; add `{ HeroEnum::Madara, <his map number> }` with the hero.
namespace ArenaBosses
{
	struct Entry
	{
		const char *hero;
		int map;
	};

	inline const std::vector<Entry> &all()
	{
		static const std::vector<Entry> table = {
			{ HeroEnum::Orochimaru, 5 },
			{ HeroEnum::Itachi, 6 },
			{ HeroEnum::Pain, 7 },
			// { HeroEnum::Madara, 8 },
		};
		return table;
	}
}

// Duels: a 1 VS 1 arena match where each side has three heroes.
//
// The player picks a main hero plus two bench heroes; the enemy gets a random
// bench of the same size. Only one hero per side is on the field. Swapping,
// forced swaps on death and the end of the match are handled by DuelSystem,
// this class only sets the match up: level, HP, chakra and the roster names.
//
// Arcade (Impl/Arcade.hpp) builds on this class, so the few places where it
// differs are exposed as protected virtual functions.
class ModeDuels : public IGameModeHandler
{
public:
	void init() override
	{
		CCLOG("Enter Duels mode.");

		hudHooked = false;

		gd.isHardCore = true;
		gd.enableGear = false;
		setSkipFlogInit(true);
	}

	void onInitHeros() override
	{
		initHeros(1, 1, nullptr, Group::Konoha);

		// This handler outlives a single match, so start from a clean slate.
		playerBench.clear();
		enemyBench.clear();

		if (selectLayer->_com1Select)
			playerBench.push_back(selectLayer->_com1Select);
		if (selectLayer->_com2Select)
			playerBench.push_back(selectLayer->_com2Select);

		arrangeEnemy();

		if (enemyGetsBench())
		{
			// Same size as the player's bench; never repeats a duelist, a bench
			// hero from either side, or a reserved hero.
			vector<string> taken = playerBench;
			for (const auto &hero : getHerosArray())
				taken.push_back(hero.name);
			for (const auto &name : reservedHeroes())
				taken.push_back(name);

			for (size_t i = 0; i < playerBench.size(); ++i)
			{
				string pick = getRandomHeroExceptAll(taken);
				enemyBench.push_back(pick);
				taken.push_back(pick);
			}
		}

		onRostersChosen();
	}

	void onGameStart() override {}
	void onGameOver() override {}

	void onCharacterInit(CharacterBase *c) override
	{
		auto layer = getGameLayer();
		if (hudHooked || layer->isHUDInit())
			return;
		hudHooked = true;

		// Safe to touch the layer here: this only runs from inside
		// GameLayer::addHero(), so it exists. The rosters go in before the
		// layer spawns the bench (GameLayer::initHeros calls spawnRoster()).
		auto &duel = layer->duel();
		duel.side(true).bench = playerBench;
		duel.side(false).bench = enemyBench;
		duel.setLoadout([this](Hero *hero, bool playerSide)
						{ equip(hero, playerSide); });
	}

	void onCharacterDead(CharacterBase *c) override
	{
		// A death does not end the match: DuelSystem swaps in the next hero
		// and reports when a side has none left (see onSideEliminated).
		getGameLayer()->queueDuelDeath(c);
	}

	void onCharacterReborn(CharacterBase *c) override {}

	vector<int> getReservedArenaMaps() override
	{
		vector<int> maps;
		for (const auto &boss : ArenaBosses::all())
			maps.push_back(boss.map);
		return maps;
	}

	vector<string> getExtraPreloadChars() override
	{
		vector<string> all = playerBench;
		all.insert(all.end(), enemyBench.begin(), enemyBench.end());
		return all;
	}

protected:
	// How much the levelled-up max HP is multiplied by.
	virtual uint32_t hpMultiplier(bool playerSide) { return 2; }
	// Runs right after initHeros(): may force or re-roll the enemy duelist.
	virtual void arrangeEnemy() {}
	virtual bool enemyGetsBench() { return true; }
	// Heroes that must never appear as a regular enemy or enemy bench member.
	virtual vector<string> reservedHeroes() { return {}; }
	// Both benches are decided; nothing more to pick.
	virtual void onRostersChosen() {}

	bool hudHooked = false;
	vector<string> playerBench;
	vector<string> enemyBench;

	// The same loadout for every hero of both sides, field and bench alike.
	void equip(Hero *hero, bool playerSide)
	{
		hero->setCoin(3000);
		hero->setEXP(2500);
		for (int i = 1; i < 6; i++)
			hero->changeHPbar();

		// Chakra is not per hero here: both sides share one pool that starts
		// empty (Systems/DuelSystem.hpp). Clear what levelling up handed out
		// and let the pool decide what is ready.
		hero->setCKR(0);
		hero->setCKR2(0);

		uint32_t maxHP = hero->getMaxHP() * hpMultiplier(playerSide);
		hero->setMaxHPValue(maxHP, false);
		hero->setHPValue(maxHP, true);

		getGameLayer()->duel().syncFlags(hero, playerSide);

		// No auto-reborn: dying is handled by the DuelSystem instead.
		hero->enableReborn = false;

		if (hero->isPlayer())
		{
			auto hud = getGameLayer()->getHudLayer();
			hud->setEXPLose();
			hud->coinLabel->setString(to_cstr(hero->getCoin()));
			if (!hero->isEnableSkill04())
				hud->skill4Button->setLock();
			if (!hero->isEnableSkill05())
				hud->skill5Button->setLock();
		}
	}
};
