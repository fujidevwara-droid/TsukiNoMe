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
		// Alive but not usable right now: a hero Ino has possessed (it is fighting
		// for the other side) or an Ino left standing as a husk. Neither can be
		// swapped to, and neither counts as the next hero in line.
		std::unordered_set<std::string> unavailable;
		std::unordered_map<std::string, Hero *> heroes;

		bool hasFallen(const std::string &key) const { return fallen.count(key) > 0; }
		bool isUnavailable(const std::string &key) const { return unavailable.count(key) > 0; }

		int firstLivingSlot() const
		{
			for (size_t i = 0; i < bench.size(); ++i)
			{
				if (!hasFallen(bench[i]) && !isUnavailable(bench[i]))
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

	// The enemy hero in play changed form: its portrait in the HUD has to follow.
	// (The human's own portrait is refreshed by Hero::setID through
	// updateHudSkillButtons; nothing else showed the enemy's name.)
	void onHeroRenamed(Hero *hero)
	{
		if (!hero || hero != activeHero(false))
			return;
		if (auto hud = _owner->getHudLayer())
			hud->refreshEnemyAvatar(hero->getName());
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
		{
			if (!isCaptive(entry.second))
				syncFlags(entry.second, playerSide);
		}
		// A possessed hero fights for the other side and draws on that side's pool.
		for (auto &capture : _captures)
		{
			if (isRosterMember(true, capture.captive) != playerSide)
				syncFlags(capture.captive, playerSide);
		}

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
			if (hero && hero != husk(playerSide) && !isCaptive(hero) && !hero->_isBenched &&
				hero->getState() != State::DEAD)
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
		auto *controlled = static_cast<Hero *>(_owner->currentPlayer);
		if (slot < 0 || slot >= static_cast<int>(s.bench.size()) || !controlled || controlled->getState() == State::DEAD)
			return;

		// While steering a hero that the human's own Ino possessed, the hero in play
		// is Ino (she stands by, defenceless). Swapping then takes her off the field
		// the way an enemy Ino's swap does: she stays as a husk, what she possessed
		// hides, and the ally comes in where the human was looking.
		Hero *outgoing = controlled;
		for (const auto &capture : _captures)
		{
			if (capture.captive == controlled && isRosterMember(true, capture.captor))
				outgoing = capture.captor;
		}
		if (outgoing->getState() == State::DEAD)
			return;

		const std::string incomingKey = s.bench[slot];
		Hero *incoming = findLiving(s, incomingKey);
		if (!incoming || incoming == outgoing)
			return;

		const std::string outgoingKey = keyOf(outgoing, true);
		// Order matters: activate() reads where the hero it replaces stands.
		activate(incoming, controlled->getPosition(), controlled->_isFlipped, true);

		if (isPossessing(outgoing))
			makeHusk(true, outgoing, outgoingKey);
		else
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

		// An Ino that is mid-possession cannot simply vanish: she stays on the
		// field, still hittable, as a husk until the possession ends or she dies.
		// Only what she possessed disappears.
		if (isPossessing(outgoing))
			makeHusk(false, outgoing, outgoingKey);
		else
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
			if (!s.hasFallen(s.bench[i]) && !s.isUnavailable(s.bench[i]))
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

	// ---- possession (Ino's Shintenshin) -----------------------------------
	// Ino can take over the hero that is in play on the other side, from either
	// side. The victim's side does not lose control of the match: it brings in its
	// next hero at once. The possessed hero stays on the field and fights for
	// Ino's team (steered by the human when the human's Ino cast it, by the AI
	// otherwise) until the possession ends, then goes back to its owner's bench.
	//
	// Returns false when the possession must not happen: the victim's side has no
	// other hero to bring in, or the target is already in an unusual state.
	// Returns true for heroes that are not part of an arena roster, which keep the
	// normal possession.
	bool beginPossession(Hero *captive, Hero *captor)
	{
		const bool victimIsPlayer = isRosterMember(true, captive);
		if (!victimIsPlayer && !isRosterMember(false, captive))
			return true;
		if (isCaptive(captive) || captive == husk(victimIsPlayer) || captive->_isBenched)
			return false;

		Side &s = side(victimIsPlayer);
		const int slot = s.firstLivingSlot();
		if (slot < 0)
			return false;

		const std::string captiveKey = keyOf(captive, victimIsPlayer);
		const std::string incomingKey = s.bench[slot];
		Hero *incoming = findLiving(s, incomingKey);
		if (!incoming)
			return false;

		if (victimIsPlayer)
		{
			// Out of the human's hands first, so activate() sees nobody to replace.
			captive->setRole(Role::Com);
			captive->_isAI = true;
			captive->_isHoldingChakraCharge = false;
		}
		activate(incoming, captive->getPosition(), captive->_isFlipped, victimIsPlayer);

		// The captive takes the slot the replacement came from, shown as unavailable.
		s.bench[slot] = captiveKey;
		s.unavailable.insert(captiveKey);
		_captures.push_back({ captive, captor });

		if (victimIsPlayer)
		{
			refreshPlayerHud(incoming, incomingKey);
			if (auto hud = _owner->getHudLayer())
				hud->onDuelPlayerSwapped(slot, captiveKey, false);
			captive->doAI(); // it fights for Ino from here on
		}
		else
		{
			showEnemy(slot, incomingKey, captiveKey, incoming);
		}
		return true;
	}

	bool isPossessing(const Hero *captor) const
	{
		for (const auto &capture : _captures)
		{
			if (capture.captor == captor)
				return true;
		}
		return false;
	}

	bool isCaptive(const Hero *hero) const
	{
		for (const auto &capture : _captures)
		{
			if (capture.captive == hero)
				return true;
		}
		return false;
	}

	Hero *husk(bool playerSide) const { return _husks[playerSide ? 1 : 0]; }

	// The possession is over (it ran out, Ino was hit out of it, or she died):
	// everything she held goes back to its owner's bench, and an Ino that was
	// being kept on the field as a husk goes to her own bench.
	//
	// Ino calls this at the END of her own cleanup. Her loops already hand control
	// back and restore the team of a possessed hero that is standing in the open;
	// this covers what they cannot reach (a hero that was hidden meanwhile) and
	// does the arena side of it: the bench, the slots and the husk.
	void releaseCapturedBy(Hero *captor)
	{
		const bool captorIsPlayer = isRosterMember(true, captor);

		for (size_t i = 0; i < _captures.size();)
		{
			if (_captures[i].captor != captor)
			{
				++i;
				continue;
			}

			Hero *captive = _captures[i].captive;
			_captures.erase(_captures.begin() + i);

			const bool captiveIsPlayer = isRosterMember(true, captive);
			side(captiveIsPlayer).unavailable.erase(keyOf(captive, captiveIsPlayer));

			captive->_isControlled = false;
			captive->setController(nullptr);
			if (captive->getGroup() != homeGroup(captiveIsPlayer))
				captive->changeGroup();
			captive->refreshTeamBarColor(); // changeGroup() may have picked the wrong colour (see CharacterBase.h)

			// Already hidden if its side swapped away mid-possession.
			if (captive->getState() != State::DEAD && !captive->_isBenched)
				bench(captive);
		}

		releaseHusk(captor);
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
			auto *deadHero = static_cast<Hero *>(death.hero);

			// The side comes from the roster, not from the hero's group: a hero
			// Ino possessed fights for the other group but is still its
			// owner's, and its death is its owner's loss.
			const bool playerSide = isRosterMember(true, deadHero);
			Side &s = side(playerSide);

			const std::string deadKey = keyOf(deadHero, playerSide);
			s.fallen.insert(deadKey);
			s.heroes.erase(deadKey);
			s.unavailable.erase(deadKey);

			// A possessed hero killed by its owner's enemies, or an Ino husk killed
			// by the player, was not the hero in play: nobody steps in for it, the
			// side just loses that hero.
			const bool wasHusk = deadHero == husk(playerSide);
			const bool wasCaptive = endCaptureOfCaptive(deadHero);
			if (wasHusk)
			{
				releaseCapturedBy(deadHero); // what she held returns (normally already done by her death)
				clearHusk(playerSide);
			}
			if (wasHusk || wasCaptive)
			{
				remove(deadHero);
				if (!playerSide)
					refreshEnemyBenchIcon(deadKey);
				continue;
			}

			int nextSlot = s.firstLivingSlot();
			// Nobody else left to send in, but Ino's husk is still standing: she is
			// the last hero and takes over instead of the side being out.
			if (nextSlot < 0 && husk(playerSide))
			{
				const std::string huskKey = keyOf(husk(playerSide), playerSide);
				auto at = std::find(s.bench.begin(), s.bench.end(), huskKey);
				if (at != s.bench.end())
				{
					nextSlot = static_cast<int>(at - s.bench.begin());
					s.unavailable.erase(huskKey);
					clearHusk(playerSide);
				}
			}

			// Nobody else left to send in, but a hero of this side may still be alive
			// in an Ino's grip. The side is not out while that is true: the
			// possession breaks and that hero takes the field where it stands.
			// (Otherwise killing the last free hero of the side that Ino is holding
			// one of would count as wiping the whole side.)
			Hero *takenBack = nullptr;
			if (nextSlot < 0)
				takenBack = takeBackCaptive(playerSide, nextSlot);

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
			if (takenBack)
				activate(incoming, takenBack->getPosition(), takenBack->_isFlipped, playerSide);
			else
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

	struct Capture
	{
		Hero *captive; // the possessed hero
		Hero *captor;  // the Ino holding it
	};

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
	std::vector<Capture> _captures;
	Hero *_husks[2] = { nullptr, nullptr }; // [0] enemy, [1] player: an Ino left on the field while her side plays another hero
	CharacterBase *_heldPlayer = nullptr; // see resolveDeaths()
	float _enemySwapCountdown = kEnemyFirstSwapWait;
	uint32_t _chakra[2] = { 0, 0 }; // [0] enemy, [1] player

	void clearHusk(bool playerSide)
	{
		Hero *&slot = _husks[playerSide ? 1 : 0];
		if (slot)
			slot->_isArenaHusk = false;
		slot = nullptr;
	}

	// The group a roster's heroes belong to when nothing is possessed.
	Group homeGroup(bool playerSide) const
	{
		if (playerSide)
			return _owner->playerGroup;
		return _owner->playerGroup == Group::Konoha ? Group::Akatsuki : Group::Konoha;
	}

	// Leaves `ino` standing on the field, hittable but inert, while her side plays
	// another hero. What she possessed hides; if the human was steering it, the
	// steering ends here.
	void makeHusk(bool playerSide, Hero *ino, const std::string &key)
	{
		clearHusk(playerSide);
		_husks[playerSide ? 1 : 0] = ino;
		ino->_isArenaHusk = true;

		// Nobody drives her any more (the flag also keeps her AI quiet).
		if (ino->isPlayer())
			ino->setRole(Role::Com);
		ino->_isControlled = false;
		ino->_isAI = false;
		ino->unschedule(schedule_selector(CharacterBase::setAI));

		side(playerSide).unavailable.insert(key);

		for (const auto &capture : _captures)
		{
			if (capture.captor != ino)
				continue;
			if (_owner->controlChar == capture.captive)
				_owner->controlChar = nullptr;
			bench(capture.captive);
		}
	}

	// Takes a husk off the field once her possession is over.
	void releaseHusk(Hero *ino)
	{
		for (bool playerSide : { true, false })
		{
			if (husk(playerSide) != ino || ino->getState() == State::DEAD)
				continue;

			// A dead husk stays marked until resolveDeaths() has seen her death (it
			// needs to know she was the husk and not the hero in play, and Ino's
			// death calls this first).
			clearHusk(playerSide);
			side(playerSide).unavailable.erase(keyOf(ino, playerSide));
			bench(ino);
		}
	}

	bool isRosterMember(bool playerSide, const Hero *hero) const
	{
		for (const auto &entry : side(playerSide).heroes)
		{
			if (entry.second == hero)
				return true;
		}
		return false;
	}

	// If `hero` was a possessed hero: ends its possession (Ino returns to normal)
	// and forgets the record. The caller removes the hero.
	bool endCaptureOfCaptive(Hero *hero)
	{
		for (size_t i = 0; i < _captures.size(); ++i)
		{
			if (_captures[i].captive != hero)
				continue;

			Hero *captor = _captures[i].captor;
			_captures.erase(_captures.begin() + i);

			// Ino's own cleanup, minus the part that needs the hero alive.
			captor->setActionResume();
			if (captor->getState() != State::DEAD && captor->getState() != State::IDLE)
				captor->idle();

			releaseHusk(captor);
			return true;
		}
		return false;
	}

	// Finds a living hero of `playerSide` that Ino currently holds, ends that
	// possession (control and team go back to normal through Ino's own cleanup)
	// and reports its bench slot in `slot`. Returns the hero, or null if there is
	// none. The caller puts it on the field.
	Hero *takeBackCaptive(bool playerSide, int &slot)
	{
		Side &s = side(playerSide);
		for (size_t i = 0; i < _captures.size(); ++i)
		{
			Hero *captive = _captures[i].captive;
			if (captive->getState() == State::DEAD || !isRosterMember(playerSide, captive))
				continue;

			const std::string key = keyOf(captive, playerSide);
			auto at = std::find(s.bench.begin(), s.bench.end(), key);
			if (at == s.bench.end())
				continue;

			Hero *captor = _captures[i].captor;
			_captures.erase(_captures.begin() + i);
			s.unavailable.erase(key);

			// Ino's own cleanup: hands control back to her and undoes the team switch
			// of a possessed hero standing in the open. Its last step reports to
			// releaseCapturedBy(), which finds nothing left to do for this hero.
			captor->setActionResume();
			if (captor->getState() != State::DEAD && captor->getState() != State::IDLE)
				captor->idle();

			// A hero that was hidden meanwhile is out of her reach: make sure.
			captive->_isControlled = false;
			captive->setController(nullptr);
			if (captive->getGroup() != homeGroup(playerSide))
				captive->changeGroup();

			releaseHusk(captor);
			slot = static_cast<int>(at - s.bench.begin());
			return captive;
		}
		return nullptr;
	}

	// Redraws the enemy HUD after a bench hero died on its own (Ino's husk).
	void refreshEnemyBenchIcon(const std::string &deadKey)
	{
		Side &s = side(false);
		auto at = std::find(s.bench.begin(), s.bench.end(), deadKey);
		Hero *active = activeHero(false);
		auto hud = _owner->getHudLayer();
		if (hud && active && at != s.bench.end())
		{
			hud->onDuelEnemySwapped(static_cast<int>(at - s.bench.begin()), keyOf(active, false), deadKey,
									active->getHpPercent());
		}
	}

	Hero *findLiving(Side &s, const std::string &key)
	{
		if (s.hasFallen(key) || s.isUnavailable(key))
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
			// A hero that comes back from a possession was running AI for Ino.
			hero->unschedule(schedule_selector(CharacterBase::setAI));
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
			hero->refreshTeamBarColor(); // its bar may still carry the colour of whoever held it
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
