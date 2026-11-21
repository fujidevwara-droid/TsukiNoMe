#pragma once
#include "Defines.h"
#include "CharacterBase.h"
#include "GameLayer.h"
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

// The XML commands
//
//     <e type="setTrapMap">mapname, transitionstyle, seconds, outsideSeconds, debuff, dynamic, buffNumber</e>
//     <e type="setBanishMap">mapname, transitionstyle, seconds, outsideSeconds, debuff, dynamic, buffNumber</e>
//     <e type="toggleTrapDimension">cooldown, cbuff</e>
//     <e type="setAllyEnter">monName</e>
//     <e type="setMonOnTrapped">monName</e>
//
// mapname  a name or a number, always from Maps/Duels/ (Maps/Duels/<mapname>.tmx, with
//          map_bg<mapname>.png next to it), in the normal game as well as in Duels/Arcade.
//          A named map ("KamuiDimension") is never picked as a random duel map; a number
//          is handy for trying a trap on an existing arena.
// style    0 = at once, 1 = fade to black, move, fade back in (the same going back),
//          2 = the screen twists into a spiral and sinks into the dark, then comes back out
//          of it (Systems/ScreenSwirl.hpp). The transition only plays when the human is among
//          those being moved.
// seconds  how long the trap lasts (default 10).
// outsideSeconds  optional. Missing, empty or 0: no limit of its own. Otherwise, while the hero
//          that made the trap is OUTSIDE the trap map, the trap map closes (everybody goes back)
//          once the hero has been outside this long. "seconds" never stops counting down. Going
//          back in stops the outside countdown (only what is left of "seconds" counts), coming
//          out starts it again from the full number; if less than that is left of "seconds",
//          "seconds" simply ends the trap first. With setBanishMap the hero starts outside, so
//          the countdown starts with the trap; with setTrapMap it starts when the hero comes out.
// debuff   read and ignored for now: empty or 0 means none.
// dynamic  optional. Missing, empty or 0: the map is always Maps/Duels/<mapname>.tmx and the
//          caught are placed on its spawn points. 1: the map follows the battle's mode, the
//          way setMapChange does (Maps/<mapname>.tmx in the normal game, Maps/Duels/<mapname>.tmx
//          in Duels/Arcade; if the normal-size file does not exist the duel one is used), the
//          trap stage runs by the same rules as that folder (a normal-size trap map scrolls),
//          and everybody lands at the position on the trap map that matches where they stood
//          on the battle's map, instead of on spawn points. "Matches" is proportional: the
//          same fraction across and up the playable area, so equal-size maps give the very
//          same coordinates and a smaller or bigger trap map squeezes or stretches the
//          positions. Followers keep their offset from their owner. Only the hero that made
//          the trap leaves by the matching position (the timer ending, or toggleTrapDimension
//          taking it out): it comes back to the position on the battle's map that matches where
//          it stands on the trap map. The caught enemies are not mapped back: they are spat
//          out around that hero (kSpitDistance, alternating sides, each one further out), with
//          their followers keeping their offset from them. If the hero is gone or dead by then,
//          or an enemy is no longer on the trap map, it goes to where it was caught. With 0,
//          everybody returns to exactly where they were caught.
// buffNumber  optional. Missing, empty or 0: nothing. Otherwise a cBuff (the same kind setBuff2
//          gives) that the hero that made the trap has only while it is INSIDE the trap map:
//          it gets it when it goes in (setTrapMap: at the start; setBanishMap: when
//          toggleTrapDimension takes it in), loses it at once when it comes out (toggleTrapDimension,
//          the trap ending, its death) and gets it again each time it goes back in. The caught
//          enemies never get it. The trap keeps the number, so toggleTrapDimension needs none.
//
// setTrapMap: the heroes the attack hits are taken into the trap map together with the hero
// that used it, and their summons, clones, puppets and mons. setBanishMap is the same
// except that the hero that used it stays where it is: only the enemies it hit are sent
// away. The trap map is a stage of its own beside the battle (Systems/Stage.hpp) and runs by
// the duel arena rules whatever the game mode; the battle keeps going on the main stage.
//
// toggleTrapDimension: the hero that made a trap map (either kind) goes into it if it is
// outside and comes out of it if it is inside, with its own summons; it goes back to where it
// last went in (dynamic: to the position that matches where it is inside the trap map). Only the hero that made the trap can do this, and only while it lasts.
// "cooldown" (seconds) then becomes the cooldown of the skill that used it, instead of the
// skill's own. Without a trap map of its own going, the command does not touch anything of
// the trap and acts like a skill-change buff (setBuff with the "cbuff" number) and the skill
// keeps its normal cooldown. See CharacterBase::toggleTrapDimension. When the trap has a
// buffNumber, going in gives the hero that buff and coming out takes it away (see above).
//
// setAllyEnter (only half done, on purpose): while the trap lasts, a random ALLY of the hero
// that made it, from outside the trap map, is pulled inside together with its summons, clones,
// puppets and mons. monName is optional (empty = no show): a mon spawned on both sides for
// the look of it. Outside it appears at the ally's position on the battle's map, inside at the
// unit that used the command: the hero itself, or the mon it was used from (that mon's
// position). The ally arrives at that same spot (when the user of the command is outside the
// trap map, at its side's spawn point, dynamic: the matching position instead). Allies are
// never "caught" victims: they do not keep the trap going and they are put back where they
// were pulled from when the trap ends. No ally outside (or none that can be pulled: dead,
// benched, hidden) = nothing happens. It can be used again for the next ally. Only the hero
// that made the trap (or its mons) can use it, and only while the trap lasts. TODO(duels): in
// Duels/Arcade it will do something else; benched heroes are never pulled, so for now there
// it finds nobody. See TrapSystem::pullAlly.
//
// setMonOnTrapped: while the trap lasts, one monName mon is spawned on EVERY enemy still caught
// inside the trap map, at that enemy's exact position (no offset). The mons belong to the hero
// that made the trap and act on its side, so what they do (a hit, a trap, an effect) is up to
// their own XML. Only the hero that made the trap (or its mons) can use it, only while the trap
// lasts; with no monName, no trap going or no enemy left inside, nothing happens. Each use
// spawns a new mon on each enemy.
//
// Who is caught: the command arms the hero for a short while (kArmSeconds), and counts the
// hits from just before it too (kLookbackSeconds), so it does not matter much whether the
// tag sits a little before or after the frame that hits. Every enemy hero the hero's attacks
// hit in that time (its own, or its projectiles') is caught; when the first one is caught
// the rest of the window is cut short (kGraceSeconds) so a multi-hit or area attack gets its
// victims together. If nothing was hit, nothing happens.
// Towers, flogs, benched heroes and heroes an attack could not damage are never caught.
//
// Caught enemies are not just let go: as they come out they are launched away from the hero that
// made the trap (AirHurt, then KnockDown, see stunReleased), so the caster has time to act.
//
// Everybody goes back when the time is up, or all caught enemies are gone (dead, or
// respawned at their base), or the hero that made the trap is gone (dead). Each caught hero
// goes back to where it was when it was caught, and the hero that made the trap to where it
// last went in. In Duels/Arcade a hero dying inside ends the trap at once, and its
// replacement comes in where the fallen hero was caught. While a trap is going on, hero
// swaps in Duels/Arcade are refused and no second trap can start.
//
// The log (CCLOG) tells what it is doing: "setTrapMap: ..." lines say armed / caught /
// started / why it did not, and why and how it ended.
class TrapSystem
{
public:
	static constexpr float kArmSeconds = 0.6f;		// how long an armed hero catches what it hits
	static constexpr float kGraceSeconds = 0.25f;	// what is left of that after the first catch
	static constexpr float kLookbackSeconds = 0.5f; // hits this recent still count when the command arrives
	static constexpr float kSpitDistance = 96.0f;	// dynamic: the first caught enemy lands this far from the caster...
	static constexpr float kSpitStep = 64.0f;		// ...and each further pair this much further out, alternating sides
	static constexpr float kDefaultSeconds = 10.0f;
	static constexpr float kReleaseFloat = 64.0f;	// the caught enemies are launched like an f_hit when they come out (see stunReleased)

	// style 1: how long the screen takes to go black, stays black and comes back.
	static constexpr float kFadeInSeconds = 0.27f;
	static constexpr float kHoldSeconds = 0.08f;
	static constexpr float kFadeOutSeconds = 0.27f;

	explicit TrapSystem(GameLayer *owner) : _owner(owner) {}
	~TrapSystem()
	{
		releaseEntries();
		releaseCaster();
	}

	// A trap is starting, going on, or ending.
	bool isActive() const { return _phase != Phase::Idle; }

	// The XML command, from the hero that used it. banish: the hero stays out of it.
	void arm(CharacterBase *caster, const string &mapName, int style, float seconds, int /*debuff*/, bool banish = false, bool dynamic = false, int buff = 0, float outsideSeconds = 0.0f)
	{
		if (!caster || mapName.empty() || !caster->isPlayerOrCom())
		{
			CCLOG("setTrapMap: ignored, the one using it must be a hero (map '%s')", mapName.c_str());
			return;
		}
		if (isActive())
		{
			CCLOG("setTrapMap: ignored, a trap is already going on");
			return;
		}
		for (const auto &arm : _arms)
		{
			if (arm.caster() == caster)
				return; // already armed
		}

		Arm armed;
		armed.units.pushBack(caster);
		armed.name = mapName;
		armed.style = style;
		armed.banish = banish;
		armed.dynamic = dynamic;
		armed.buff = buff;
		armed.outsideSeconds = (std::max)(0.0f, outsideSeconds);
		armed.seconds = seconds > 0.0f ? seconds : kDefaultSeconds;
		armed.windowLeft = kArmSeconds;

		// Whoever it hit a moment ago counts too.
		for (const auto &recent : _recent)
		{
			CharacterBase *hitter = recent.units.at(0);
			CharacterBase *victim = recent.units.at(1);
			if (hitter == caster && canCatch(caster, victim) &&
				std::find(armed.units.begin(), armed.units.end(), victim) == armed.units.end())
			{
				armed.units.pushBack(victim);
				armed.windowLeft = (std::min)(armed.windowLeft, kGraceSeconds);
				CCLOG("setTrapMap: caught %s (hit just before)", victim->getName().c_str());
			}
		}

		CCLOG("setTrapMap: %s armed (%s), map '%s' style %d for %.1fs, %d caught so far",
			  caster->getName().c_str(), banish ? "banish" : "trap", mapName.c_str(), style, armed.seconds,
			  (int)armed.victimCount());
		_arms.push_back(armed);
	}

	// From CharacterBase::setDamage once a hit has landed: `caster` is whose attack it was
	// (the attacker's owner), `victim` who was hit.
	void onHit(CharacterBase *caster, CharacterBase *victim)
	{
		if (!caster || !victim || caster == victim || isActive())
			return;
		if (!caster->isPlayerOrCom() || !victim->isPlayerOrCom() || !canCatch(caster, victim))
			return;

		remember(caster, victim);

		for (auto &arm : _arms)
		{
			if (arm.caster() != caster)
				continue;
			if (std::find(arm.units.begin(), arm.units.end(), victim) != arm.units.end())
				return;

			arm.units.pushBack(victim);
			arm.windowLeft = (std::min)(arm.windowLeft, kGraceSeconds);
			CCLOG("setTrapMap: caught %s", victim->getName().c_str());
			return;
		}
	}

	// toggleTrapDimension: `who` goes into its own trap map if it is outside, out of it if it
	// is inside.
	TrapToggle toggleDimension(CharacterBase *who)
	{
		if (!isActive() || !who || who != _caster)
			return TrapToggle::None; // no trap of its own (another hero's cannot be entered)

		if (_phase != Phase::Active || _swapWaiting || !casterInPlay(who))
			return TrapToggle::Busy;

		const bool goingIn = !_casterInside;
		CCLOG("toggleTrapDimension: %s goes %s", who->getName().c_str(), goingIn ? "in" : "out");

		_humanInvolved = who == _owner->currentPlayer;
		run(_style, [this, goingIn]()
			{
				if (isActive() && _phase == Phase::Active && casterInPlay(_caster))
					moveCaster(goingIn);
			});
		return TrapToggle::Toggled;
	}

	// setMonOnTrapped: one `monName` mon on each enemy caught inside, at its exact position.
	void monOnTrapped(CharacterBase *executor, const string &monName)
	{
		if (!executor || monName.empty())
			return;
		if (!isActive() || _phase != Phase::Active || !casterInPlay(_caster))
		{
			CCLOG("setMonOnTrapped: ignored, no trap map of this hero is going on");
			return;
		}
		if (!ownedByCaster(executor))
		{
			CCLOG("setMonOnTrapped: ignored, only the hero that made the trap (or its mons) can use it");
			return;
		}

		int spawnedCount = 0;
		for (const auto &entry : _entries)
		{
			CharacterBase *enemy = entry.unit;
			if (entry.kind != Kind::Victim || !stillValid(enemy) || enemy->_stageId != _stageId)
				continue;
			// Exactly where the enemy stands.
			_caster->spawnEffectMon(monName, enemy->getPosition(), _stageId, executor->_isFlipped, _caster);
			++spawnedCount;
		}
		CCLOG("setMonOnTrapped: %d mon(s) spawned", spawnedCount);
	}

	// setAllyEnter: `executor` (the hero that made the trap, or one of its mons) pulls a random
	// ally from outside the trap map in. See the top of this file.
	//
	// TODO(duels): only the normal-game half is coded. In Duels/Arcade this will do something
	// else; the place for it is here, before the candidates are looked for. Meanwhile the
	// allies there are benched (never pulled), so it finds nobody.
	void pullAlly(CharacterBase *executor, const string &monName)
	{
		if (!executor)
			return;
		if (!isActive() || _phase != Phase::Active || _swapWaiting || !casterInPlay(_caster))
		{
			CCLOG("setAllyEnter: ignored, no trap map of this hero is going on (or it is changing)");
			return;
		}
		if (!ownedByCaster(executor))
		{
			CCLOG("setAllyEnter: ignored, only the hero that made the trap (or its mons) can use it");
			return;
		}

		vector<CharacterBase *> candidates;
		CCObject *obj = nullptr;
		CCARRAY_FOREACH(_owner->getChildren(), obj)
		{
			auto unit = dynamic_cast<CharacterBase *>(obj);
			if (unit && canPull(unit))
				candidates.push_back(unit);
		}
		if (candidates.empty())
		{
			CCLOG("setAllyEnter: no ally outside the trap map to pull in");
			return;
		}
		CharacterBase *ally = candidates[rand() % candidates.size()];

		// Where the command was used, now: a mon of the hero may be gone by the time the screen
		// has faded.
		const bool executorInside = executor->_stageId == _stageId;
		const Vec2 executorPosition = executor->getPosition();
		const bool executorFlipped = executor->_isFlipped;

		CCLOG("setAllyEnter: %s is pulled in", ally->getName().c_str());
		_humanInvolved = ally == _owner->currentPlayer;
		Vector<CharacterBase *> held; // keeps the ally alive while the screen fades
		held.pushBack(ally);
		run(_style, [this, held, monName, executorInside, executorPosition, executorFlipped]()
			{
				if (!isActive() || _phase != Phase::Active || !casterInPlay(_caster))
					return;
				CharacterBase *chosen = held.at(0);
				if (canPull(chosen))
					enterAlly(chosen, monName, executorInside, executorPosition, executorFlipped);
			});
	}

	// Every frame (GameLayer::updateViewPoint).
	void tick(float dt)
	{
		for (size_t i = 0; i < _recent.size();)
		{
			_recent[i].age += dt;
			if (_recent[i].age > kLookbackSeconds)
				_recent.erase(_recent.begin() + i);
			else
				++i;
		}

		// Armed heroes whose window has closed.
		for (size_t i = 0; i < _arms.size();)
		{
			_arms[i].windowLeft -= dt;
			if (_arms[i].windowLeft > 0.0f)
			{
				++i;
				continue;
			}

			Arm arm = std::move(_arms[i]);
			_arms.erase(_arms.begin() + i);
			begin(arm);
		}

		if (_swapWaiting)
		{
			_swapTimer -= dt;
			if (_swapTimer <= 0.0f)
			{
				_swapWaiting = false;
				auto action = std::move(_swapAction);
				_swapAction = nullptr;
				if (action)
					action();
			}
			return;
		}

		if (_phase != Phase::Active)
			return;

		_timer -= dt;

		// The outside countdown only runs while the hero that made the trap is outside.
		bool outsideUp = false;
		if (!_casterInside && _outsideTimer > 0.0f)
		{
			_outsideTimer -= dt;
			outsideUp = _outsideTimer <= 0.0f;
		}

		if (_timer <= 0.0f || outsideUp || casterGone() || noVictimLeft())
		{
			CCLOG("setTrapMap: ending (%s)", _timer <= 0.0f ? "time is up" : outsideUp ? "the hero stayed outside too long" : casterGone() ? "the hero that made it is gone" : "no caught enemy is left");
			_phase = Phase::Leaving;
			_humanInvolved = _casterInside && _caster == _owner->currentPlayer;
			for (const auto &entry : _entries)
				_humanInvolved = _humanInvolved || entry.unit == _owner->currentPlayer;
			run(_style, [this]()
				{ eject(); });
		}
	}

	// From DuelSystem::resolveDeaths: `dead` has died. If it was in the trap business, the trap
	// ends now (nothing may be left waiting while a replacement steps in) and the position where
	// its replacement should appear is where it was caught, on the battle's map (dynamic: around
	// the hero that made the trap, where the caught are spat out; for the hero itself, the
	// position matching where it fell on the trap map).
	Vec2 onHeroDeath(CharacterBase *dead, const Vec2 &position)
	{
		if (!isActive())
			return position;

		bool member = false;
		Vec2 back = position;
		if (dead == _caster)
		{
			member = true;
			if (_casterInside)
				back = exitPosition(dead, position, _casterSaved); // outside, it fell on the battle's map already
		}
		else
		{
			int spat = 0; // victims before it that eject() will spit out first
			for (const auto &entry : _entries)
			{
				if (entry.unit == dead && entry.kind == Kind::Victim)
				{
					member = true;
					back = entry.position;
					Vec2 anchor;
					if (_dynamic && dead->_stageId == _stageId && casterAnchor(anchor))
						back = spitPosition(anchor, spat);
				}
				else if (entry.kind == Kind::Victim && entry.unit->getParent() && entry.unit->_stageId == _stageId)
				{
					++spat;
				}
			}
		}
		if (!member)
			return position;

		CCLOG("setTrapMap: ending, %s died", dead->getName().c_str());
		if (_phase == Phase::Entering)
			cancelPending();
		else
			eject(true);
		return back;
	}

private:
	enum class Phase
	{
		Idle,
		Entering,
		Active,
		Leaving
	};
	enum class Kind
	{
		Victim,
		Follower,
		Ally // pulled in by setAllyEnter: goes back where it was pulled from
	};

	struct Arm
	{
		string name;
		int style = 0;
		bool banish = false;
		bool dynamic = false; // 5th field of the XML command: see the top of this file
		int buff = 0;		  // last field: the cBuff the caster has while it is inside (0 = none)
		float outsideSeconds = 0.0f; // 4th field: the trap's life while the caster is outside (0 = no limit)
		float seconds = kDefaultSeconds;
		float windowLeft = 0.0f;
		// [0] is the hero that used the command, the rest are who it has caught. A Vector
		// retains what it holds: a hero can be freed within the window (a duel death).
		Vector<CharacterBase *> units;

		CharacterBase *caster() const { return units.at(0); }
		size_t victimCount() const { return units.size() - 1; }
	};

	struct Recent
	{
		Vector<CharacterBase *> units; // [0] hit [1]; retained
		float age = 0.0f;
	};

	void remember(CharacterBase *caster, CharacterBase *victim)
	{
		for (auto &recent : _recent)
		{
			if (recent.units.at(0) == caster && recent.units.at(1) == victim)
			{
				recent.age = 0.0f;
				return;
			}
		}

		if (_recent.size() >= 16)
			_recent.erase(_recent.begin()); // the oldest

		Recent recent;
		recent.units.pushBack(caster);
		recent.units.pushBack(victim);
		_recent.push_back(recent);
	}

	struct Entry
	{
		CharacterBase *unit = nullptr; // retained while the trap lasts
		Kind kind = Kind::Victim;
		Vec2 position; // where it was on the battle's map
	};

	// An ally the hero that made the trap can pull in: a hero of its side, outside the trap map,
	// alive, in play (not benched or hidden).
	bool canPull(CharacterBase *ally) const
	{
		return _caster && ally != _caster && stillValid(ally) &&
			   ally->isPlayerOrCom() && ally->getGroup() == _caster->getGroup() &&
			   ally->_isVisable && !ally->_isBenched && !ally->_isArenaHusk &&
			   ally->_stageId == _fromStageId && !isFollowerRole(ally);
	}

	// `unit` is the hero that made the trap, or belongs to it (its mons and summons).
	bool ownedByCaster(CharacterBase *unit) const
	{
		for (int depth = 0; unit && depth < 4; ++depth)
		{
			if (unit == _caster)
				return true;
			unit = unit->getMaster();
		}
		return false;
	}

	// Moves `ally` (and what belongs to it) into the trap map and spawns the show mons.
	void enterAlly(CharacterBase *ally, const string &monName, bool executorInside, const Vec2 &executorPosition, bool executorFlipped)
	{
		GameLayer &game = *_owner;
		const Stage &stage = game.stageOf(_stageId);

		const Vec2 outsidePosition = ally->getPosition(); // where it stood on the battle's map
		const bool outsideFlipped = ally->_isFlipped;

		int pulled = 0;
		for (const auto &entry : _entries)
			pulled += entry.kind == Kind::Ally ? 1 : 0;

		// It arrives at the unit that used the command; if that one is not inside the trap map,
		// at its side's spawn point (dynamic: at the matching position).
		Vec2 arrival;
		if (executorInside)
			arrival = executorPosition;
		else if (_dynamic)
			arrival = equivalentPosition(game.stageOf(_fromStageId), stage, outsidePosition);
		else
			arrival = slotPosition(stage, ally->getGroup(), 1 + pulled);

		std::unordered_map<CharacterBase *, Vec2> destinations;
		destinations[ally] = arrival;
		const auto followers = followersOf(_fromStageId, destinations); // before anything moves

		addEntry(ally, Kind::Ally);
		for (const auto &follower : followers)
			addEntry(follower.first, Kind::Follower);

		place(ally, _stageId, arrival);
		for (const auto &follower : followers)
			place(follower.first, _stageId, follower.second);

		// The show: one mon where it left from, one where it arrives.
		if (!monName.empty())
		{
			_caster->spawnEffectMon(monName, outsidePosition, _fromStageId, outsideFlipped, _caster);
			_caster->spawnEffectMon(monName, arrival, _stageId, executorFlipped, _caster);
		}

		game.refreshMinimap();
		CCLOG("setAllyEnter: %s is inside stage %d with %d followers", ally->getName().c_str(), _stageId, (int)followers.size());
	}

	bool canCatch(CharacterBase *caster, CharacterBase *victim) const
	{
		return victim->isPlayerOrCom() &&
			   victim->getGroup() != caster->getGroup() &&
			   victim->getState() != State::DEAD &&
			   victim->_isVisable && !victim->_isBenched && !victim->_isArenaHusk &&
			   victim->_stageId == caster->_stageId;
	}

	static bool isFollowerRole(CharacterBase *unit)
	{
		return unit->isClone() || unit->isKugutsu() || unit->isSummon() || unit->isMon();
	}

	// The spawn points of a duel-style map ("object" layer: 1-3 Konoha, 4-6 Akatsuki).
	static Vec2 slotPosition(const Stage &stage, Group group, int index)
	{
		const int slot = (group == Group::Akatsuki ? MapPosCount : 0) + (index % MapPosCount);

		if (auto objects = stage.map->objectGroupNamed("object"))
		{
			if (auto array = objects->getObjects())
			{
				if (slot < (int)array->count())
				{
					auto dict = (CCDictionary *)array->objectAtIndex(slot);
					const int x = ((CCString *)dict->objectForKey("x"))->intValue();
					const int y = ((CCString *)dict->objectForKey("y"))->intValue();
					return Vec2(stage.originX + x, y);
				}
			}
		}

		// A map without spawn points: either side of the middle.
		const Size size = stage.pixels();
		return Vec2(stage.originX + size.width * (group == Group::Akatsuki ? 0.65f : 0.35f), size.height / 2);
	}

	// The playable x range of a stage in GameLayer coordinates: between the walls in an arena,
	// the whole map otherwise.
	std::pair<float, float> playableX(const Stage &stage) const
	{
		if (stage.arena)
		{
			const auto bounds = _owner->arenaBounds(stage);
			return { bounds.minX, bounds.maxX };
		}
		return { stage.originX, stage.originX + stage.pixels().width };
	}

	// Dynamic mode: where `position` (on stage `from`) is on stage `to`, proportionally. The
	// same fraction across the playable width and up the map height; equal maps give the same
	// local coordinates, and the result is always inside the trap map's playable area.
	Vec2 equivalentPosition(const Stage &from, const Stage &to, const Vec2 &position) const
	{
		const auto fromX = playableX(from);
		const auto toX = playableX(to);

		float fx = fromX.second > fromX.first ? (position.x - fromX.first) / (fromX.second - fromX.first) : 0.5f;
		float fy = from.pixels().height > 0.0f ? position.y / from.pixels().height : 0.5f;
		fx = (std::min)((std::max)(fx, 0.0f), 1.0f);
		fy = (std::min)((std::max)(fy, 0.0f), 1.0f);

		return Vec2(toX.first + fx * (toX.second - toX.first), fy * to.pixels().height);
	}

	// Where a unit goes when it leaves the trap map: dynamic, and it is on the trap map: the
	// matching position on the battle's map, from where it stands now (`current`). Otherwise
	// (not dynamic, or it is not on the trap map any more) the position saved when it went in.
	Vec2 exitPosition(const CharacterBase *unit, const Vec2 &current, const Vec2 &saved) const
	{
		if (!_dynamic || unit->_stageId != _stageId)
			return saved;
		return equivalentPosition(_owner->stageOf(_stageId), _owner->stageOf(_fromStageId), current);
	}

	// Dynamic: where the hero that made the trap will be on the battle's map once the trap is
	// over (where it would come out if it is inside, where it stands if it is already outside).
	// False if it is gone, dead, or not on the battle's map.
	bool casterAnchor(Vec2 &anchor) const
	{
		if (!stillValid(_caster))
			return false;
		if (_casterInside)
		{
			anchor = equivalentPosition(_owner->stageOf(_stageId), _owner->stageOf(_fromStageId), _caster->getPosition());
			return true;
		}
		if (_caster->_stageId != _fromStageId)
			return false;
		anchor = _caster->getPosition();
		return true;
	}

	// Dynamic: where the index-th caught enemy lands around `anchor`: alternating sides, each
	// pair a step further out, kept inside the battle map's playable width.
	Vec2 spitPosition(const Vec2 &anchor, int index) const
	{
		const float side = (index % 2 == 0) ? 1.0f : -1.0f;
		const float x = anchor.x + side * (kSpitDistance + (index / 2) * kSpitStep);
		const auto range = playableX(_owner->stageOf(_fromStageId));
		return Vec2((std::min)((std::max)(x, range.first), range.second), anchor.y);
	}

	bool humanIsAmong(const vector<CharacterBase *> &units) const
	{
		return _owner->currentPlayer &&
			   std::find(units.begin(), units.end(), _owner->currentPlayer) != units.end();
	}

	static bool stillValid(CharacterBase *unit)
	{
		return unit && unit->getParent() && unit->getState() != State::DEAD;
	}

	// A hero that made the trap must still be in play: not benched meanwhile (Duels/Arcade).
	static bool casterInPlay(CharacterBase *caster)
	{
		return stillValid(caster) && !caster->_isBenched && caster->_isVisable;
	}

	void holdCaster(CharacterBase *caster)
	{
		releaseCaster();
		_caster = caster;
		_caster->retain();
	}

	void releaseCaster()
	{
		if (_caster)
			_caster->release();
		_caster = nullptr;
		_casterInside = false;
		_dynamic = false;
		_buff = 0;
		_buffOn = false;
		_outsideLimit = 0.0f;
		_outsideTimer = 0.0f;
	}

	// An armed hero's window has closed: start the trap if anything was caught.
	void begin(const Arm &arm)
	{
		if (arm.victimCount() == 0)
		{
			CCLOG("setTrapMap: %s hit nobody, nothing happens", arm.caster()->getName().c_str());
			return;
		}
		if (isActive() || !casterInPlay(arm.caster()))
		{
			CCLOG("setTrapMap: not started (%s)", isActive() ? "a trap is already going on" : "the hero that used it is gone or benched");
			return;
		}

		// Which map file, and whether the trap stage runs by the arena rules (which follow the
		// folder, as in setMapChange). Not dynamic: always the duel map. Dynamic: the folder
		// that suits the battle, falling back to the duel map if there is no normal-size one.
		bool arena = true;
		string file = format("Maps/Duels/{}.tmx", arm.name);
		if (arm.dynamic && !_owner->stageOf(arm.caster()->_stageId).arena)
		{
			const string normal = format("Maps/{}.tmx", arm.name);
			if (FileUtils::sharedFileUtils()->isFileExist(normal))
			{
				file = normal;
				arena = false;
			}
			else
			{
				CCLOG("setTrapMap: no normal-size map %s, using the duel map", normal.c_str());
			}
		}
		if (!FileUtils::sharedFileUtils()->isFileExist(file))
		{
			CCLOG("setTrapMap: map %s not found", file.c_str());
			return;
		}
		CCLOG("setTrapMap: starting (%s), map %s, %d caught", arm.banish ? "banish" : "trap", file.c_str(), (int)arm.victimCount());

		// Those who move: the caught, and the hero that used it unless it banishes.
		vector<CharacterBase *> involved;
		for (size_t i = arm.banish ? 1 : 0; i < arm.units.size(); ++i)
			involved.push_back(arm.units.at(i));

		holdCaster(arm.caster()); // from here on this hero is the owner of the trap
		_phase = Phase::Entering;
		_style = arm.style;
		_humanInvolved = humanIsAmong(involved);
		run(arm.style, [this, arm, file, arena]()
			{ enter(arm, file, arena); });
	}

	// Runs `action` at once, or behind a transition (style 1 fade, style 2 spiral) if the
	// human is moving. Its own timer, not the veil's, decides when, so another veil replacing
	// this one on screen cannot leave the trap hanging.
	void run(int style, std::function<void()> action)
	{
		if (style == 1 && _humanInvolved)
		{
			_owner->_veil.triggerFade(0, 0, 0, 255, kFadeInSeconds, kHoldSeconds, kFadeOutSeconds);
			_swapWaiting = true;
			_swapTimer = kFadeInSeconds;
			_swapAction = std::move(action);
		}
		else if (style == 2 && _humanInvolved)
		{
			_owner->startSwirl(ScreenSwirl::kInSeconds, ScreenSwirl::kHoldSeconds, ScreenSwirl::kOutSeconds);
			_swapWaiting = true;
			_swapTimer = ScreenSwirl::kInSeconds;
			_swapAction = std::move(action);
		}
		else
		{
			action();
		}
	}

	void cancelPending()
	{
		endCasterBuff();
		_swapWaiting = false;
		_swapAction = nullptr;
		_phase = Phase::Idle;
		releaseCaster();
	}

	// The followers (clones, summons, puppets, mons) of the units in `destinations`, found on
	// stage `from`: each with where it goes, next to where its owner goes. A few passes, so that
	// the summon of a summon is found too. Call it before the owners are moved: the offsets are
	// taken from where they stand now.
	vector<std::pair<CharacterBase *, Vec2>> followersOf(int from, const std::unordered_map<CharacterBase *, Vec2> &destinations)
	{
		vector<std::pair<CharacterBase *, Vec2>> found;
		std::unordered_map<CharacterBase *, Vec2> placed = destinations;

		vector<CharacterBase *> candidates;
		CCObject *obj = nullptr;
		CCARRAY_FOREACH(_owner->getChildren(), obj)
		{
			auto unit = dynamic_cast<CharacterBase *>(obj);
			if (unit && unit->_stageId == from && isFollowerRole(unit) && !placed.count(unit))
				candidates.push_back(unit);
		}

		for (int pass = 0; pass < 3; ++pass)
		{
			bool grew = false;
			for (auto unit : candidates)
			{
				if (placed.count(unit))
					continue;
				auto owner = unit->getMaster();
				if (!owner)
					continue;
				auto at = placed.find(owner);
				if (at == placed.end())
					continue;

				Vec2 offset = unit->getPosition() - owner->getPosition();
				offset.x = (std::max)(-48.0f, (std::min)(48.0f, offset.x));
				offset.y = (std::max)(-24.0f, (std::min)(24.0f, offset.y));
				const Vec2 where = at->second + offset;
				placed[unit] = where;
				found.push_back({ unit, where });
				grew = true;
			}
			if (!grew)
				break;
		}
		return found;
	}

	void enter(const Arm &arm, const string &file, bool arena)
	{
		GameLayer &game = *_owner;

		// Things may have changed during the transition.
		CharacterBase *caster = arm.caster();
		if (!casterInPlay(caster))
		{
			CCLOG("setTrapMap: cancelled, the hero that used it is gone");
			cancelPending();
			return;
		}
		vector<CharacterBase *> victims;
		for (size_t i = 1; i < arm.units.size(); ++i)
		{
			CharacterBase *victim = arm.units.at(i);
			if (stillValid(victim) && canCatch(caster, victim))
				victims.push_back(victim);
		}
		if (victims.empty())
		{
			CCLOG("setTrapMap: cancelled, nobody caught is still there to take along");
			cancelPending();
			return;
		}

		Stage *stage = game.addStage(arm.name, file, arena);
		if (!stage)
		{
			CCLOG("setTrapMap: map %s could not be loaded", file.c_str());
			cancelPending();
			return;
		}

		releaseEntries();
		_fromStageId = caster->_stageId;
		_stageId = stage->id;
		_timer = arm.seconds;
		_casterInside = false;
		_casterSaved = caster->getPosition();
		_dynamic = arm.dynamic;
		_buff = arm.buff;
		_buffOn = false;
		_outsideLimit = arm.outsideSeconds;
		_outsideTimer = 0.0f;

		// The caught, each on its own side's spawn points (dynamic: at the matching position
		// on the trap map), and what belongs to them.
		std::unordered_map<CharacterBase *, Vec2> destinations;
		int next[2] = { 0, 0 };
		for (auto victim : victims)
		{
			const int side = victim->getGroup() == Group::Akatsuki ? 1 : 0;
			destinations[victim] = _dynamic
									   ? equivalentPosition(game.stageOf(_fromStageId), *stage, victim->getPosition())
									   : slotPosition(*stage, victim->getGroup(), next[side]++);
			addEntry(victim, Kind::Victim);
		}
		for (const auto &follower : followersOf(_fromStageId, destinations))
		{
			destinations[follower.first] = follower.second;
			addEntry(follower.first, Kind::Follower);
		}
		for (const auto &entry : _entries)
			place(entry.unit, stage->id, destinations[entry.unit]);

		_phase = Phase::Active;

		// setTrapMap takes the hero that used it along; setBanishMap leaves it where it is.
		if (!arm.banish)
			moveCaster(true);
		else
			startOutsideCountdown(); // the hero starts outside
		game.refreshMinimap();
		CCLOG("setTrapMap: inside stage %d, %d units moved, %.1fs", stage->id, (int)_entries.size(), _timer);
	}

	// The hero that made the trap goes in (to its side's first spawn point, or the matching position
	// when dynamic, remembering where it
	// was) or out (to where it last went in), together with its own summons.
	void moveCaster(bool in)
	{
		if (!casterInPlay(_caster))
			return;

		Vec2 destination = exitPosition(_caster, _caster->getPosition(), _casterSaved);
		if (in)
		{
			_casterSaved = _caster->getPosition();
			destination = _dynamic
							  ? equivalentPosition(_owner->stageOf(_fromStageId), _owner->stageOf(_stageId), _casterSaved)
							  : slotPosition(_owner->stageOf(_stageId), _caster->getGroup(), 0);
		}

		std::unordered_map<CharacterBase *, Vec2> owners;
		owners[_caster] = destination;
		const auto followers = followersOf(_caster->_stageId, owners);

		const int to = in ? _stageId : _fromStageId;
		place(_caster, to, destination);
		for (const auto &follower : followers)
			place(follower.first, to, follower.second);

		_casterInside = in;

		// The dimension's buff: on while inside, off the moment it is outside. Lasts as long as the
		// trap has left; endCasterBuff() takes it away earlier whenever the hero comes out.
		if (in)
		{
			giveCasterBuff();
			_outsideTimer = 0.0f; // inside: only what is left of the main lifespan counts
		}
		else
		{
			endCasterBuff();
			startOutsideCountdown();
		}
		_owner->refreshMinimap();
	}

	// The hero that made the trap is outside: the trap map has `_outsideLimit` seconds left to
	// live (0 = no limit of its own). The main lifespan keeps counting down on its own, so when
	// less than that is left of it, it ends the trap first.
	void startOutsideCountdown()
	{
		_outsideTimer = _outsideLimit > 0.0f ? _outsideLimit : 0.0f;
	}

	void giveCasterBuff()
	{
		if (_buff <= 0 || !_caster)
			return;
		endCasterBuff(); // never stack it: it starts afresh each time the hero goes in
		_caster->startCBuff(_buff, (std::max)(_timer, 0.0f));
		_buffOn = true;
	}

	void endCasterBuff()
	{
		if (!_buffOn)
			return;
		_buffOn = false;
		if (_caster)
			_caster->endCBuff();
	}

	void addEntry(CharacterBase *unit, Kind kind)
	{
		Entry entry;
		entry.unit = unit;
		entry.kind = kind;
		entry.position = unit->getPosition();
		unit->retain();
		_entries.push_back(entry);
	}

	// The hero that made the trap is dead or gone from the scene. (Not "outside the trap
	// map": it may leave and come back through toggleTrapDimension.)
	bool casterGone() const { return !stillValid(_caster); }

	// All caught enemies are dead, or back at their base, or otherwise off the trap map.
	bool noVictimLeft() const
	{
		for (const auto &entry : _entries)
		{
			if (entry.kind == Kind::Victim && stillValid(entry.unit) && entry.unit->_stageId == _stageId)
				return false;
		}
		return true;
	}

	// Everybody goes back to where they were, and the trap map goes away. `forced` (a hero
	// died in Duels/Arcade): no waiting for the screen to go black, the map is changed on the
	// spot and the screen only fades in from black afterwards.
	void eject(bool forced = false)
	{
		GameLayer &game = *_owner;

		bool humanWasTrapped = false;
		std::unordered_map<CharacterBase *, Vec2> restored;
		std::unordered_map<CharacterBase *, Vec2> before; // where they stood on the trap map

		// Dynamic: where the hero that made the trap ends up; the caught are spat out around it.
		Vec2 anchor;
		const bool haveAnchor = _dynamic && casterAnchor(anchor);

		for (const auto &entry : _entries)
		{
			if (entry.unit->getParent())
				before[entry.unit] = entry.unit->getPosition();
		}

		endCasterBuff(); // whatever way the trap ends, the dimension's buff goes with it

		// The hero that made the trap, if it is inside, goes back first (to where it last went in;
		// dynamic: to the position matching where it is now), so the caught can land around it.
		if (_caster && _casterInside && _caster->getParent())
		{
			const Vec2 back = exitPosition(_caster, _caster->getPosition(), _casterSaved);
			place(_caster, _fromStageId, back);
			restored[_caster] = back;
			if (_caster == game.currentPlayer)
				humanWasTrapped = true;
		}

		int spat = 0;
		for (const auto &entry : _entries)
		{
			if (!entry.unit->getParent())
				continue; // gone from the scene meanwhile

			Vec2 back = entry.position;
			if (haveAnchor && entry.unit->_stageId == _stageId)
			{
				if (entry.kind == Kind::Victim)
				{
					back = spitPosition(anchor, spat++);
				}
				else if (entry.kind == Kind::Follower)
				{
					// A follower keeps its offset from its owner.
					auto owner = entry.unit->getMaster();
					auto landed = owner ? restored.find(owner) : restored.end();
					auto was = owner ? before.find(owner) : before.end();
					if (landed != restored.end() && was != before.end())
						back = landed->second + (before[entry.unit] - was->second);
				}
			}
			place(entry.unit, _fromStageId, back);
			restored[entry.unit] = back;
			if (entry.unit == game.currentPlayer)
				humanWasTrapped = true;
			if (entry.kind == Kind::Victim)
				stunReleased(entry.unit);
		}

		// What is still on the trap map (summons, clones, projectiles) follows its owner back,
		// or lands where the hero that made the trap ends up (not dynamic: where it went in).
		CCObject *obj = nullptr;
		CCARRAY_FOREACH(game.getChildren(), obj)
		{
			auto unit = dynamic_cast<CharacterBase *>(obj);
			if (!unit || unit->_stageId != _stageId)
				continue;

			Vec2 where = haveAnchor ? anchor : _casterSaved;
			auto owner = unit->getMaster();
			auto found = owner ? restored.find(owner) : restored.end();
			if (found != restored.end())
				where = found->second;
			place(unit, _fromStageId, where);
		}

		CCLOG("setTrapMap: back on the battle's map");
		game.removeStage(_stageId);
		releaseEntries();
		releaseCaster();
		_phase = Phase::Idle;
		_swapWaiting = false;
		_swapAction = nullptr;
		game.refreshMinimap();

		if (forced && humanWasTrapped)
		{
			// The cut was instant: reveal it (from the dark, or out of the spiral).
			if (_style == 1)
				game.showVeilFade(0.0f, 0.1f, kFadeOutSeconds);
			else if (_style == 2)
				game.startSwirl(0.0f, 0.05f, ScreenSwirl::kOutSeconds);
		}
	}

	// A caught enemy that comes out of the trap map is thrown into the air (AirHurt, then it falls
	// into KnockDown), away from the hero that made the trap: a fair stun that keeps the caster
	// safe. Armored / sticking / dead enemies are left alone, floatUP refuses them.
	void stunReleased(CharacterBase *victim)
	{
		if (!victim->getParent() || victim->getState() == State::DEAD)
			return;

		// Face the caster, so the launch (which goes behind the unit) sends it away from the caster.
		if (_caster && _caster->getParent())
		{
			const bool faceLeft = _caster->getPositionX() < victim->getPositionX(); // flipped = facing left
			if (victim->_isFlipped != faceLeft)
			{
				victim->setFlipX(faceLeft);
				victim->_isFlipped = faceLeft;
			}
		}

		victim->floatUP(kReleaseFloat, true);
		victim->airHurt(); // does nothing unless floatUP got it into the air
	}

	void place(CharacterBase *unit, int stageId, const Vec2 &position)
	{
		unit->_stageId = stageId;
		unit->setPosition(position);
		unit->setDesiredPosition(position); // or its next step would pull it back
	}

	void releaseEntries()
	{
		for (auto &entry : _entries)
			entry.unit->release();
		_entries.clear();
	}

	GameLayer *_owner;

	vector<Arm> _arms;
	vector<Recent> _recent;

	Phase _phase = Phase::Idle;
	vector<Entry> _entries; // the caught and what belongs to them
	CharacterBase *_caster = nullptr; // the hero that made the trap; retained while it lasts
	bool _casterInside = false;
	Vec2 _casterSaved; // where the hero that made the trap stood on the battle's map when it last went in
	int _style = 0;
	bool _dynamic = false; // this trap was armed with the dynamic field
	int _buff = 0;		   // this trap's buffNumber: the cBuff its caster has while inside (0 = none)
	bool _buffOn = false;  // the caster has that buff right now
	bool _humanInvolved = false;
	int _fromStageId = kMainStage;
	int _stageId = kMainStage;
	float _timer = 0.0f;
	float _outsideLimit = 0.0f; // this trap's outsideSeconds (0 = no limit of its own)
	float _outsideTimer = 0.0f; // what is left of it while the caster is outside (0 = not running)

	bool _swapWaiting = false;
	float _swapTimer = 0.0f;
	std::function<void()> _swapAction;
};
