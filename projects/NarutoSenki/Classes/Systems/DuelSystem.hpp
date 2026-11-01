#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Core/Hero.hpp"
#include "Systems/DuelChakra.hpp"
#include "GameLayer.h"
#include "HudLayer.h"

// Roster, bench and death handling for the 1v1 arena modes (Duels, Arcade).
//
// Each side fields three heroes, one on the field and two waiting on a bench.
// Everybody is spawned once at the start of the match and stays spawned: a
// switch only toggles which hero is benched (hidden, untargetable, frozen) and
// which one is active. A hero that dies is removed for good and its side
// brings in the next living bench member; a side is out when none is left.
//
// Heroes are tracked by the name they were spawned with (their "roster key"),
// not by getName(), because that changes when a hero transforms.
class DuelSystem
{
public:
	// Called for every hero of both sides once the HUD exists (active heroes
	// and bench members alike), so one function sets up both.
	using Loadout = std::function<void(Hero *hero, bool playerSide)>;

	struct Side
	{
		// Slot-ordered names of who is waiting. When a hero swaps in, its slot
		// is relabelled with whoever left the field, so a slot always shows
		// the hero that is currently benched there (or the one that fell).
		std::vector<std::string> bench;
		std::unordered_set<std::string> fallen;
		std::unordered_map<std::string, Hero *> heroes;

		bool hasFallen(const std::string &key) const { return fallen.count(key) > 0; }

		int firstLivingSlot() const
		{
			for (size_t i = 0; i < bench.size(); ++i)
			{
				if (!hasFallen(bench[i]))
					return static_cast<int>(i);
			}
			return -1;
		}
	};

	struct DeathReport
	{
		bool matchOver = false;
		bool playerSideOut = false;
	};

	explicit DuelSystem(GameLayer *owner) : _owner(owner) {}

	~DuelSystem()
	{
		if (_heldPlayer)
			_heldPlayer->release();
	}

	DuelSystem(const DuelSystem &) = delete;
	DuelSystem &operator=(const DuelSystem &) = delete;

	// ---- setup ------------------------------------------------------------
	Side &side(bool playerSide) { return _sides[playerSide ? 1 : 0]; }
	const Side &side(bool playerSide) const { return _sides[playerSide ? 1 : 0]; }

	void setLoadout(Loadout loadout) { _loadout = std::move(loadout); }

	// Registers the two active heroes, spawns both benches (hidden from the
	// first frame) and schedules the loadout for everyone once the HUD is up.
	void spawnRoster()
	{
		auto *player = static_cast<Hero *>(_owner->currentPlayer);
		if (!player)
			return;

		for (auto &s : _sides)
			s.heroes.clear();
		_benched.clear();

		const Group playerGroup = player->getGroup();
		const Group enemyGroup = playerGroup == Group::Konoha ? Group::Akatsuki : Group::Konoha;

		Hero *enemy = nullptr;
		for (auto *hero : _owner->_CharacterArray)
		{
			if (hero != player && hero->isCom() && hero->getGroup() == enemyGroup && hero->isNotGuardian())
			{
				enemy = hero;
				break;
			}
		}

		side(true).heroes[player->getName()] = player;
		if (enemy)
			side(false).heroes[enemy->getName()] = enemy;

		std::vector<std::pair<Hero *, bool>> benchedNow;
		int charId = kFirstBenchCharId; // distinct ids so the minimap can tell them apart

		auto spawnBench = [&](bool playerSide, Hero *anchor, Group group)
		{
			if (!anchor)
				return;
			Side &s = side(playerSide);
			for (const auto &name : s.bench)
			{
				if (name.empty())
					continue;

				Hero *hero = _owner->addHero(name, Role::Com, group, anchor->getPosition(), charId++);
				if (!hero)
					continue;

				hero->enableReborn = false;
				hero->enterBench(); // never visible, targetable or AI-driven, not even for a frame
				s.heroes[name] = hero;
				_benched.push_back(hero);
				benchedNow.emplace_back(hero, playerSide);
			}
		};
		spawnBench(true, player, playerGroup);
		spawnBench(false, enemy, enemyGroup);

		// Everyone stays in _CharacterArray until the HUD has built the
		// minimap (one icon per entry). Right after that the benched heroes
		// leave it for good, which is what hides them from every "loop over
		// all heroes" targeting / area / buff routine.
		_owner->onHUDInitialized(
			[this, benchedNow]()
			{
				if (_loadout)
				{
					for (bool playerSide : { true, false })
					{
						for (auto &entry : side(playerSide).heroes)
							_loadout(entry.second, playerSide);
					}
				}
				for (auto &entry : benchedNow)
					std::erase(_owner->_CharacterArray, entry.first);
			});
	}

	// ---- shared chakra pool ----------------------------------------------
	// One pool per side (see Systems/DuelChakra.hpp). Every hero of a side
	// reads and spends the same pool, so a swap never changes what is ready.
	bool isPlayerSide(CharacterBase *hero) const
	{
		return hero && hero->getGroup() == _owner->playerGroup;
	}

	uint32_t chakra(bool playerSide) const { return _chakra[playerSide ? 1 : 0]; }

	void setChakra(bool playerSide, uint32_t value)
	{
		_chakra[playerSide ? 1 : 0] = (std::min)(value, DuelChakra::kMax);

		// The ougis flags are what the AI (and some skills) look at, so every
		// hero of the side - bench included - must agree with the pool.
		for (auto &entry : side(playerSide).heroes)
			syncFlags(entry.second, playerSide);

		if (auto hud = _owner->getHudLayer())
		{
			hud->setDuelChakraBarUI(playerSide);
			if (playerSide)
			{
				// setCKRLose() is the only thing that redraws the skill 4/5
				// dials, and every pool change ends up here.
				hud->setCKRLose(false);
				hud->setCKRLose(true);
			}
		}
	}

	void addChakra(bool playerSide, uint32_t amount) { setChakra(playerSide, chakra(playerSide) + amount); }

	// Casting never fails halfway: a cost larger than the pool just empties it.
	void spendChakra(bool playerSide, uint32_t cost)
	{
		const uint32_t pool = chakra(playerSide);
		setChakra(playerSide, pool >= cost ? pool - cost : 0);
	}

	void syncFlags(CharacterBase *hero, bool playerSide)
	{
		if (!hero)
			return;
		const uint32_t pool = chakra(playerSide);
		hero->_isCanOugis1 = pool >= DuelChakra::kSkill4Cost;
		hero->_isCanOugis2 = pool >= DuelChakra::kSkill5Cost;
	}

	// ---- queries ----------------------------------------------------------
	std::string keyOf(Hero *hero, bool playerSide)
	{
		for (auto &entry : side(playerSide).heroes)
		{
			if (entry.second == hero)
				return entry.first;
		}
		return hero ? hero->getName() : std::string();
	}

	Hero *activeHero(bool playerSide)
	{
		for (auto &entry : side(playerSide).heroes)
		{
			Hero *hero = entry.second;
			if (hero && !hero->_isBenched && hero->getState() != State::DEAD)
				return hero;
		}
		return nullptr;
	}

	// Everything that has to be unloaded at teardown: the field plus the bench.
	std::vector<Hero *> allHeroes() const
	{
		std::vector<Hero *> all = _owner->_CharacterArray;
		all.insert(all.end(), _benched.begin(), _benched.end());
		return all;
	}

	// ---- switching --------------------------------------------------------
	// The human swaps to the hero waiting in bench slot `slot`.
	void swapPlayer(int slot)
	{
		Side &s = side(true);
		auto *outgoing = static_cast<Hero *>(_owner->currentPlayer);
		if (slot < 0 || slot >= static_cast<int>(s.bench.size()) || !outgoing || outgoing->getState() == State::DEAD)
			return;

		const std::string incomingKey = s.bench[slot];
		Hero *incoming = findLiving(s, incomingKey);
		if (!incoming || incoming == outgoing)
			return;

		const std::string outgoingKey = keyOf(outgoing, true);
		// Order matters: activate() reads where the outgoing hero stands.
		activate(incoming, outgoing->getPosition(), outgoing->_isFlipped, true);
		bench(outgoing);

		s.bench[slot] = outgoingKey;
		refreshPlayerHud(incoming, incomingKey);
		if (auto hud = _owner->getHudLayer())
			hud->onDuelPlayerSwapped(slot, outgoingKey, true);
	}

	// The AI side swaps like a player would, just without the HUD buttons.
	void swapEnemy(int slot)
	{
		Side &s = side(false);
		if (slot < 0 || slot >= static_cast<int>(s.bench.size()))
			return;

		const std::string incomingKey = s.bench[slot];
		Hero *incoming = findLiving(s, incomingKey);
		Hero *outgoing = activeHero(false);
		if (!incoming || !outgoing || incoming == outgoing)
			return;

		const std::string outgoingKey = keyOf(outgoing, false);
		activate(incoming, outgoing->getPosition(), outgoing->_isFlipped, false);
		bench(outgoing);

		s.bench[slot] = outgoingKey;
		showEnemy(slot, incomingKey, outgoingKey, incoming);
	}

	// Ticked once per match second. Never interrupts anything and is never
	// forced: it just waits out a cooldown and then swaps at a random slot.
	void tickEnemySwap(float dt)
	{
		Side &s = side(false);
		if (s.bench.empty())
			return;

		_enemySwapCountdown -= dt;
		if (_enemySwapCountdown > 0.0f || !activeHero(false))
			return;

		std::vector<int> open;
		for (size_t i = 0; i < s.bench.size(); ++i)
		{
			if (!s.hasFallen(s.bench[i]))
				open.push_back(static_cast<int>(i));
		}
		if (open.empty())
			return;

		setRand();
		swapEnemy(open[random(static_cast<int>(open.size()))]);

		setRand();
		_enemySwapCountdown = kEnemySwapMinWait + random(kEnemySwapJitter);
	}

	// ---- departing heroes -------------------------------------------------
	// Ticked by GameLayer while any hero is still finishing an action after being
	// swapped out. A hero is hidden once it is idle (or dead, or after a safety
	// timeout so a stuck state can never leave a ghost on the field).
	void tickDeparting(float dt)
	{
		for (auto it = _departing.begin(); it != _departing.end();)
		{
			it->waited += dt;
			const State state = it->hero->getState();
			if (state == State::IDLE || state == State::DEAD || it->waited >= kMaxDepartSeconds)
			{
				finishBench(it->hero);
				it = _departing.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (_departing.empty())
			_owner->unschedule(schedule_selector(GameLayer::pollDepartingHeroes));
	}

	// ---- deaths -----------------------------------------------------------
	// Deaths are queued and resolved on the next frame. onCharacterDead fires
	// as the very first line of CharacterBase::dead(), whose later body (HP
	// bar, controller cleanup...) still works on the dying hero. Swapping in a
	// replacement and refreshing the HUD right away would be overwritten by
	// that tail.
	// Only roster heroes count: a clone or puppet dying must not trigger a
	// forced switch. Returns whether the death was queued.
	bool queueDeath(CharacterBase *dead)
	{
		if (!isRosterHero(dead))
			return false;
		_pending.push_back({ dead, dead->getGroup(), dead->getPosition(), dead->_isFlipped });
		return true;
	}

	bool isRosterHero(CharacterBase *hero) const
	{
		if (!hero)
			return false;
		for (const auto &s : _sides)
		{
			for (const auto &entry : s.heroes)
			{
				if (entry.second == hero)
					return true;
			}
		}
		return false;
	}

	bool hasPendingDeaths() const { return !_pending.empty(); }

	DeathReport resolveDeaths()
	{
		// Take the queue first: ending the match can tear things down, and a
		// double death in one frame must not be handled twice.
		std::vector<PendingDeath> batch;
		batch.swap(_pending);

		DeathReport report;
		for (const auto &death : batch)
		{
			const bool playerSide = death.group == _owner->playerGroup;
			Side &s = side(playerSide);
			auto *deadHero = static_cast<Hero *>(death.hero);

			const std::string deadKey = keyOf(deadHero, playerSide);
			s.fallen.insert(deadKey);
			s.heroes.erase(deadKey);

			const int nextSlot = s.firstLivingSlot();
			const bool sideOut = nextSlot < 0;

			if (deadHero == _owner->currentPlayer)
			{
				if (sideOut && playerSide)
				{
					// The result screen still reads this hero's live stats
					// (kills, group...) after it has left the scene, so keep
					// the object alive instead of dropping the pointer.
					deadHero->retain();
					_heldPlayer = deadHero;
				}
				else
				{
					_owner->currentPlayer = nullptr; // activate() assigns the successor
				}
			}

			remove(deadHero);

			if (sideOut)
			{
				report.matchOver = true;
				report.playerSideOut = playerSide;
				return report; // do not touch the rest of the batch after a result
			}

			const std::string nextKey = s.bench[nextSlot];
			// The slot keeps the name of whoever fell there, as a record.
			s.bench[nextSlot] = deadKey;

			auto it = s.heroes.find(nextKey);
			if (it == s.heroes.end() || !it->second)
				continue;

			Hero *incoming = it->second;
			activate(incoming, death.position, death.flipped, playerSide);

			if (playerSide)
			{
				refreshPlayerHud(incoming, nextKey);
				if (auto hud = _owner->getHudLayer())
					hud->onDuelPlayerSwapped(nextSlot, deadKey, false);
			}
			else
			{
				showEnemy(nextSlot, nextKey, deadKey, incoming);
			}
		}
		return report;
	}

private:
	static constexpr int kFirstBenchCharId = 100;
	static constexpr float kEnemyFirstSwapWait = 15.0f;
	static constexpr float kEnemySwapMinWait = 20.0f;
	static constexpr int kEnemySwapJitter = 21;
	static constexpr float kMaxDepartSeconds = 6.0f;

	struct Departing
	{
		Hero *hero;
		float waited;
	};

	struct PendingDeath
	{
		CharacterBase *hero;
		Group group;
		Vec2 position;
		bool flipped;
	};

	GameLayer *_owner;
	Side _sides[2]; // [0] enemy, [1] player
	Loadout _loadout;
	std::vector<Hero *> _benched;
	std::vector<PendingDeath> _pending;
	std::vector<Departing> _departing;
	CharacterBase *_heldPlayer = nullptr; // see resolveDeaths()
	float _enemySwapCountdown = kEnemyFirstSwapWait;
	uint32_t _chakra[2] = { 0, 0 }; // [0] enemy, [1] player

	Hero *findLiving(Side &s, const std::string &key)
	{
		if (s.hasFallen(key))
			return nullptr;
		auto it = s.heroes.find(key);
		if (it == s.heroes.end() || !it->second || it->second->getState() == State::DEAD)
			return nullptr;
		return it->second;
	}

	// Takes a hero off the field. It leaves the character list (and so every
	// targeting / area routine) immediately, but it only disappears once it is
	// done with what it is doing: a hero mid-skill stays visible, untouchable and
	// uncontrolled until it returns to idle, and is then hidden and frozen.
	void bench(Hero *hero)
	{
		if (!hero)
			return;

		std::erase(_owner->_CharacterArray, hero);
		_benched.push_back(hero);

		const State state = hero->getState();
		if (state == State::IDLE || state == State::WALK || state == State::DEAD)
		{
			finishBench(hero);
			return;
		}

		hero->beginDeparture();
		_departing.push_back({ hero, 0.0f });
		_owner->scheduleFollowingPause(schedule_selector(GameLayer::pollDepartingHeroes), 0.05f, kCCRepeatForever);
	}

	// The second half of benching: hide and freeze the hero, and call off the
	// Kuchiyose it summoned.
	void finishBench(Hero *hero)
	{
		hero->setSkillEffect("smk");
		hero->enterBench();
		hero->killOwnedSummons();
	}

	// Brings a benched hero onto the field where the one it replaces stood
	// (swapped out or dead) and hands over control (human) or AI (enemy).
	void activate(Hero *hero, const Vec2 &pos, bool flipped, bool playerSide)
	{
		if (!hero)
			return;

		std::erase(_benched, hero);

		// Swapped straight back in while it was still finishing its action.
		_departing.erase(std::remove_if(_departing.begin(), _departing.end(),
										[hero](const Departing &d) { return d.hero == hero; }),
						 _departing.end());

		hero->leaveBench(pos, flipped);

		auto &field = _owner->_CharacterArray;
		if (std::find(field.begin(), field.end(), hero) == field.end())
			field.push_back(hero);

		syncFlags(hero, playerSide);

		// Heroes start with zero walk speed until the match begins; a bench
		// member was never in the field list then, so restore it here.
		hero->setWalkSpeed(hero->_originSpeed);
		hero->enableReborn = false;

		if (playerSide)
		{
			hero->setRole(Role::Player);
			hero->_isAI = false;
			_owner->currentPlayer = hero;

			// The HP bar was chosen by role at spawn (benched allies got the
			// AI style), so rebuild it for the player.
			if (hero->_hpBar)
			{
				hero->_hpBar->removeFromParent();
				hero->_hpBar = nullptr;
			}
			hero->setHPbar();
			hero->setHPValue(hero->getHP(), true);
		}
		else
		{
			hero->setRole(Role::Com);
			hero->doAI();
		}
	}

	void refreshPlayerHud(Hero *hero, const std::string &key)
	{
		// The outgoing hero may have been mid-skill when benched; its idle()
		// ran as a non-player, so make sure the HUD is not left waiting on a
		// skill that will never finish.
		_owner->setSkillFinish(true);

		auto hud = _owner->getHudLayer();
		if (!hud)
			return;

		hud->setEXPLose();
		hud->coinLabel->setString(to_cstr(hero->getCoin()));

		// Skill 4/5 locks belong to a hero, but the buttons are shared. Start from
		// clean buttons so nothing the previous hero locked (a puppet still out...)
		// leaks onto this one, then apply this hero's own: first "not available
		// yet", then whatever it keeps locked for itself.
		hud->skill4Button->unLock();
		hud->skill5Button->unLock();
		if (!hero->isEnableSkill04())
			hud->skill4Button->setLock();
		if (!hero->isEnableSkill05())
			hud->skill5Button->setLock();
		hero->reapplyOugisLocks();
		hud->initGearButton(key);
		hud->updateSkillButtons();
		hud->resetSkillButtons();
		_owner->setHPLose(hero->getHpPercent());
	}

	void showEnemy(int slot, const std::string &nowOnField, const std::string &nowBenched, Hero *hero)
	{
		if (auto hud = _owner->getHudLayer())
			hud->onDuelEnemySwapped(slot, nowOnField, nowBenched, hero->getHpPercent());
	}

	// Removes a hero that died. This is the only thing that ever despawns a
	// hero: with enableReborn off, Hero::reborn() never runs its cleanup, so
	// without this the corpse would stay on screen under a frozen skull.
	void remove(CharacterBase *dead)
	{
		if (!dead)
			return;

		dead->setSkillEffect("smk");

		auto detach = [](CharacterBase *node)
		{
			CCNotificationCenter::sharedNotificationCenter()->removeObserver(node, "acceptAttack");
			node->unscheduleAllSelectors(); // also cancels the pending reborn countdown
			node->stopAllActions();
			if (node->_shadow)
				node->_shadow->removeFromParent();
			node->removeFromParent();
		};

		// Anything still owned by the dying hero (puppets, summons) would be
		// left with a dangling master, so it goes too.
		auto &field = _owner->_CharacterArray;
		for (auto it = field.begin(); it != field.end();)
		{
			Hero *other = *it;
			if (other != dead && (other->getMaster() == dead || other->getSecMaster() == dead))
			{
				detach(other);
				it = field.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (dead->hasMonsterArrayAny())
		{
			for (auto monster : dead->getMonsterArray())
				detach(monster);
			dead->getMonsterArray().clear();
		}

		std::erase(field, static_cast<Hero *>(dead));
		detach(dead);
	}
};
