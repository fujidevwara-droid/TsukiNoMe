#pragma once
#include "Defines.h"
#include "CharacterBase.h"
#include "GameLayer.h"
#include "GameMode/GameModeImpl.h"
#include <functional>

// The XML command
//
//     <e type="setMapChange">mapname, transitionstyle, seconds</e>
//
// mapname  a name or a number: Maps/<mapname>.tmx in the normal game, Maps/Duels/<mapname>.tmx
//          in Duels and Arcade (and map_bg<mapname>.png next to it for the background).
// style    0 = change at once, 1 = fade to black, change the map behind it, fade back in
//          (the same sequence going back).
// seconds  no number (or 0): the change is permanent. Otherwise the map goes back to the
//          one it was after that many seconds, counted from when the new map is in place.
//
// This is only a change of scenery: the map and the background are swapped, and everything
// else stays exactly where it is, units included. (Taking units somewhere else is what
// setTrapMap is for.) It changes the map of the stage the caster is on, which is the
// battle's own map unless the caster is inside a trap map.
//
// The new map has to suit the units: they are not moved, so on a smaller map one that
// stands beyond the edge is pushed back in by the normal edge handling when it next
// moves. Towers and the map's own spawn points are not read again.
//
// While a change is under way (or a timed one is in effect) another setMapChange is ignored.
class MapChangeSystem
{
public:
	// style 1: how long the screen takes to go black, stays black and comes back.
	static constexpr float kFadeInSeconds = 0.27f;
	static constexpr float kHoldSeconds = 0.08f;
	static constexpr float kFadeOutSeconds = 0.27f;

	explicit MapChangeSystem(GameLayer *owner) : _owner(owner) {}

	// A change is on its way, or a timed one has not been reverted yet.
	bool isBusy() const { return _swapWaiting || _tempActive; }

	void request(int stageId, const string &mapName, int style, float seconds)
	{
		CCLOG("setMapChange: request stage %d map '%s' style %d seconds %.1f (duel mode %d)",
			  stageId, mapName.c_str(), style, seconds, isDuelMode() ? 1 : 0);

		if (mapName.empty())
			return;

		if (isBusy())
		{
			CCLOG("setMapChange: ignored, a map change is already under way");
			return;
		}

		const Stage &stage = _owner->stageOf(stageId);
		if (stage.id != stageId)
			return; // that stage is gone

		const string file = mapFile(stage, mapName);
		if (!FileUtils::sharedFileUtils()->isFileExist(file))
		{
			CCLOG("setMapChange: map %s not found", file.c_str());
			return;
		}

		run(style, [this, stageId, mapName, style, seconds]()
			{
				const string previous = _owner->stageOf(stageId).name;
				if (!swap(stageId, mapName))
					return;

				if (seconds > 0.0f)
				{
					_tempActive = true;
					_reverting = false;
					_tempTimer = seconds;
					_stageId = stageId;
					_previousName = previous;
					_style = style;
				}
			});
	}

	// Every frame (GameLayer::updateViewPoint).
	void tick(float dt)
	{
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

		if (_tempActive && !_reverting)
		{
			_tempTimer -= dt;
			if (_tempTimer <= 0.0f)
			{
				_reverting = true;
				run(_style, [this]()
					{
						swap(_stageId, _previousName); // false if the stage has gone: nothing to put back
						_tempActive = false;
						_reverting = false;
					});
			}
		}
	}

private:
	// The map file of a stage: arena stages use the Duels folder, the rest the normal one.
	static string mapFile(const Stage &stage, const string &name)
	{
		return stage.arena ? format("Maps/Duels/{}.tmx", name) : format("Maps/{}.tmx", name);
	}

	bool swap(int stageId, const string &name)
	{
		const Stage &stage = _owner->stageOf(stageId);
		if (stage.id != stageId)
			return false;

		if (!_owner->replaceStageMap(stageId, name, mapFile(stage, name)))
		{
			CCLOG("setMapChange: map %s could not be loaded", name.c_str());
			return false;
		}
		CCLOG("setMapChange: stage %d now shows map '%s'", stageId, name.c_str());
		return true;
	}

	// Runs `action` at once, or behind a fade to black (style 1): the action happens when
	// the screen is fully black. Its own timer, not the veil's, decides when, so another
	// veil replacing this one on screen cannot leave a change hanging.
	void run(int style, std::function<void()> action)
	{
		if (style == 1)
		{
			_owner->_veil.triggerFade(0, 0, 0, 255, kFadeInSeconds, kHoldSeconds, kFadeOutSeconds);
			_swapWaiting = true;
			_swapTimer = kFadeInSeconds;
			_swapAction = std::move(action);
		}
		else
		{
			action();
		}
	}

	GameLayer *_owner;

	bool _swapWaiting = false;
	float _swapTimer = 0.0f;
	std::function<void()> _swapAction;

	bool _tempActive = false;
	bool _reverting = false;
	float _tempTimer = 0.0f;
	int _style = 0;
	int _stageId = kMainStage;
	string _previousName;
};
