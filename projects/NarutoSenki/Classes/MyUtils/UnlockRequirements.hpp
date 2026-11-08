#pragma once
#include <functional>
#include <string>
#include <vector>

#include "Enums/HeroEnum.h"
#include "MyUtils/KTools.h"

// Character unlock rules.
//
// A hero is "gated" only if it has an entry in rules() below; every other hero
// is always available. Progress is stored through KTools (UnlockedChar table
// for the unlocked flag, CharRecord for the numbers the rules read), so nothing
// here keeps state of its own apart from the one-shot notification queue.
namespace Unlocks
{
	enum class Kind
	{
		DefeatArcadeBoss, // clear the boss round of Arcade against `boss`
		ArcadeRounds,     // reach a stage in Arcade with every listed hero on the team
		RegularWins,      // win a number of normal matches with every listed hero
		CoinPurchase,     // pay `cost` coins
	};

	struct Goal
	{
		const char *hero;
		int target;
	};

	struct Rule
	{
		const char *hero;
		Kind kind;
		const char *boss = nullptr; // DefeatArcadeBoss: defaults to `hero` when null
		int cost = 0;               // CoinPurchase
		std::vector<Goal> goals;    // ArcadeRounds / RegularWins
	};

	// Add a line here to gate a hero. Madara is not shipped in this build, so
	// its entry stays commented out as documentation of the rule shapes.
	// NarutoSR is a skin of Naruto on the select screen and unlocks once
	// Naruto has reached Arcade stage 20.
	inline const std::vector<Rule> &rules()
	{
		static const std::vector<Rule> table = {
			// { HeroEnum::Madara,   Kind::DefeatArcadeBoss },
			{ HeroEnum::NarutoSR, Kind::ArcadeRounds, nullptr, 0, { { HeroEnum::Naruto, 20 } } },
		};
		return table;
	}

	inline const Rule *ruleFor(const std::string &hero)
	{
		for (const auto &rule : rules())
		{
			if (hero == rule.hero)
				return &rule;
		}
		return nullptr;
	}

	inline const char *bossOf(const Rule &rule)
	{
		return rule.boss ? rule.boss : rule.hero;
	}

	// ---- unlock notifications --------------------------------------------
	// Heroes unlocked since the player last saw the results screen.
	inline std::vector<std::string> &notificationQueue()
	{
		static std::vector<std::string> queue;
		return queue;
	}

	inline std::vector<std::string> drainNotifications()
	{
		std::vector<std::string> out;
		out.swap(notificationQueue());
		return out;
	}

	// ---- state ------------------------------------------------------------
	inline bool isUnlocked(const std::string &hero)
	{
		if (!ruleFor(hero))
			return true;

		// A gated hero with no stored row yet gets an explicit "locked" row, so
		// the default for gated heroes is locked rather than KTools' "no row =
		// unlocked".
		if (!KTools::hasUnlockRecord(hero.c_str()))
		{
			KTools::lockCharacter(hero.c_str());
			return false;
		}
		return KTools::isCharacterUnlocked(hero.c_str());
	}

	inline bool goalsMet(const Rule &rule)
	{
		const bool arcade = rule.kind == Kind::ArcadeRounds;
		for (const auto &goal : rule.goals)
		{
			int progress = arcade ? KTools::readArcadeBest(goal.hero) : KTools::readWinNumFromSQL(goal.hero);
			if (progress < goal.target)
				return false;
		}
		return true;
	}

	// Whether the hero's progress requirement is currently satisfied. Boss and
	// purchase rules are never "eligible" on their own - they need the event.
	inline bool isEligible(const std::string &hero)
	{
		const Rule *rule = ruleFor(hero);
		if (!rule || isUnlocked(hero))
			return true;

		switch (rule->kind)
		{
		case Kind::ArcadeRounds:
		case Kind::RegularWins:
			return goalsMet(*rule);
		case Kind::DefeatArcadeBoss:
		case Kind::CoinPurchase:
			break;
		}
		return false;
	}

	inline void grant(const Rule &rule)
	{
		KTools::unlockCharacter(rule.hero);
		notificationQueue().push_back(rule.hero);
	}

	// ---- events -----------------------------------------------------------
	inline void onArcadeBossDefeated(const std::string &boss)
	{
		for (const auto &rule : rules())
		{
			if (rule.kind == Kind::DefeatArcadeBoss && boss == bossOf(rule) && !isUnlocked(rule.hero))
				grant(rule);
		}
	}

	// Re-checks every progress-based rule; call after records change.
	inline void onProgressChanged()
	{
		for (const auto &rule : rules())
		{
			bool progressRule = rule.kind == Kind::ArcadeRounds || rule.kind == Kind::RegularWins;
			if (progressRule && !isUnlocked(rule.hero) && goalsMet(rule))
				grant(rule);
		}
	}

	inline bool tryPurchase(const std::string &hero)
	{
		const Rule *rule = ruleFor(hero);
		if (!rule || rule->kind != Kind::CoinPurchase || isUnlocked(hero))
			return false;

		int coins = KTools::readCoinFromSQL();
		if (coins < rule->cost)
			return false;

		KTools::saveToSQLite("GameRecord", "coin", std::to_string(coins - rule->cost).c_str(), false);
		KTools::unlockCharacter(hero.c_str());
		return true;
	}

	// ---- popup text -------------------------------------------------------
	inline std::string describeGoal(Kind kind, const Goal &goal)
	{
		if (kind == Kind::ArcadeRounds)
			return format("Clear Arcade stage {} with {} on your team", goal.target, goal.hero);
		return format("Win {} matches as {}", goal.target, goal.hero);
	}

	inline std::vector<std::string> describe(const std::string &hero)
	{
		const Rule *rule = ruleFor(hero);
		if (!rule)
			return {};

		switch (rule->kind)
		{
		case Kind::DefeatArcadeBoss:
			return { format("Defeat {} in Arcade", bossOf(*rule)) };
		case Kind::CoinPurchase:
			return { format("Spend {} coins to unlock", rule->cost) };
		case Kind::ArcadeRounds:
		case Kind::RegularWins:
			break;
		}

		if (rule->goals.empty())
			return {};
		if (rule->goals.size() == 1)
			return { describeGoal(rule->kind, rule->goals[0]) };

		// Several goals: lead with a progress count, then one line per goal.
		int done = 0;
		for (const auto &goal : rule->goals)
		{
			int progress = rule->kind == Kind::ArcadeRounds ? KTools::readArcadeBest(goal.hero) : KTools::readWinNumFromSQL(goal.hero);
			if (progress >= goal.target)
				++done;
		}

		std::vector<std::string> lines = { format("{}/{} done", done, (int)rule->goals.size()) };
		for (const auto &goal : rule->goals)
			lines.push_back(describeGoal(rule->kind, goal));
		return lines;
	}

	// Newline-joined, because the Lua bridge only passes plain strings.
	inline std::string describeJoined(const std::string &hero)
	{
		std::string joined;
		for (const auto &line : describe(hero))
		{
			if (!joined.empty())
				joined += '\n';
			joined += line;
		}
		return joined;
	}
}
