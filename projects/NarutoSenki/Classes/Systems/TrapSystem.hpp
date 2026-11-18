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
//     <e type="setTrapMap">mapname, transitionstyle, seconds, debuff</e>
//     <e type="setBanishMap">mapname, transitionstyle, seconds, debuff</e>
//     <e type="toggleTrapDimension">cooldown, cbuff</e>
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
// debuff   read and ignored for now: empty or 0 means none.
//
// setTrapMap: the heroes the attack hits are taken into the trap map together with the hero
// that used it, and their summons, clones, puppets and mons. setBanishMap is the same
// except that the hero that used it stays where it is: only the enemies it hit are sent
// away. The trap map is a stage of its own beside the battle (Systems/Stage.hpp) and runs by
// the duel arena rules whatever the game mode; the battle keeps going on the main stage.
//
// toggleTrapDimension: the hero that made a trap map (either kind) goes into it if it is
// outside and comes out of it if it is inside, with its own summons; it goes back to where it
// last went in. Only the hero that made the trap can do this, and only while it lasts.
// "cooldown" (seconds) then becomes the cooldown of the skill that used it, instead of the
// skill's own. Without a trap map of its own going, the command does not touch anything of
// the trap and acts like a skill-change buff (setBuff with the "cbuff" number) and the skill
// keeps its normal cooldown. See CharacterBase::toggleTrapDimension.
//
// Who is caught: the command arms the hero for a short while (kArmSeconds), and counts the
// hits from just before it too (kLookbackSeconds), so it does not matter much whether the
// tag sits a little before or after the frame that hits. Every enemy hero the hero's attacks
// hit in that time (its own, or its projectiles') is caught; when the first one is caught
// the rest of the window is cut short (kGraceSeconds) so a multi-hit or area attack gets its
// victims together. If nothing was hit, nothing happens.
// Towers, flogs, benched heroes and heroes an attack could not damage are never caught.
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
	static constexpr float kDefaultSeconds = 10.0f;

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
	void arm(CharacterBase *caster, const string &mapName, int style, float seconds, int /*debuff*/, bool banish = false)
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
		if (_timer <= 0.0f || casterGone() || noVictimLeft())
		{
			CCLOG("setTrapMap: ending (%s)", _timer <= 0.0f ? "time is up" : casterGone() ? "the hero that made it is gone" : "no caught enemy is left");
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
	// its replacement should appear is where it was caught, on the battle's map.
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
				back = _casterSaved; // outside, it fell on the battle's map already
		}
		else
		{
			for (const auto &entry : _entries)
			{
				if (entry.unit == dead && entry.kind == Kind::Victim)
				{
					member = true;
					back = entry.position;
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
		Follower
	};

	struct Arm
	{
		string name;
		int style = 0;
		bool banish = false;
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

		const string file = format("Maps/Duels/{}.tmx", arm.name);
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
		run(arm.style, [this, arm, file]()
			{ enter(arm, file); });
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

	void enter(const Arm &arm, const string &file)
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

		Stage *stage = game.addStage(arm.name, file, true);
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

		// The caught, each on its own side's spawn points, and what belongs to them.
		std::unordered_map<CharacterBase *, Vec2> destinations;
		int next[2] = { 0, 0 };
		for (auto victim : victims)
		{
			const int side = victim->getGroup() == Group::Akatsuki ? 1 : 0;
			destinations[victim] = slotPosition(*stage, victim->getGroup(), next[side]++);
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
		game.refreshMinimap();
		CCLOG("setTrapMap: inside stage %d, %d units moved, %.1fs", stage->id, (int)_entries.size(), _timer);
	}

	// The hero that made the trap goes in (to its side's first spawn point, remembering where it
	// was) or out (to where it last went in), together with its own summons.
	void moveCaster(bool in)
	{
		if (!casterInPlay(_caster))
			return;

		Vec2 destination = _casterSaved;
		if (in)
		{
			_casterSaved = _caster->getPosition();
			destination = slotPosition(_owner->stageOf(_stageId), _caster->getGroup(), 0);
		}

		std::unordered_map<CharacterBase *, Vec2> owners;
		owners[_caster] = destination;
		const auto followers = followersOf(_caster->_stageId, owners);

		const int to = in ? _stageId : _fromStageId;
		place(_caster, to, destination);
		for (const auto &follower : followers)
			place(follower.first, to, follower.second);

		_casterInside = in;
		_owner->refreshMinimap();
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
		for (const auto &entry : _entries)
		{
			if (!entry.unit->getParent())
				continue; // gone from the scene meanwhile
			place(entry.unit, _fromStageId, entry.position);
			restored[entry.unit] = entry.position;
			if (entry.unit == game.currentPlayer)
				humanWasTrapped = true;
		}

		// The hero that made the trap, if it is inside, goes back to where it last went in.
		if (_caster && _casterInside && _caster->getParent())
		{
			place(_caster, _fromStageId, _casterSaved);
			restored[_caster] = _casterSaved;
			if (_caster == game.currentPlayer)
				humanWasTrapped = true;
		}

		// What is still on the trap map (summons, clones, projectiles) follows its owner back,
		// or lands where the hero that made the trap went in.
		CCObject *obj = nullptr;
		CCARRAY_FOREACH(game.getChildren(), obj)
		{
			auto unit = dynamic_cast<CharacterBase *>(obj);
			if (!unit || unit->_stageId != _stageId)
				continue;

			Vec2 where = _casterSaved;
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
	bool _humanInvolved = false;
	int _fromStageId = kMainStage;
	int _stageId = kMainStage;
	float _timer = 0.0f;

	bool _swapWaiting = false;
	float _swapTimer = 0.0f;
	std::function<void()> _swapAction;
};
