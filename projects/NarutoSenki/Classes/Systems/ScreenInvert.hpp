#pragma once
#include <algorithm>

// Colour inversion of the picture, driven by the character XML tags
//
//     <e type="setInvert">mode, seconds</e>
//     <e type="setInvert2">mode, frames</e>
//     <e type="setCircleInvert">mode, seconds</e>
//     <e type="setCircleInvert2">mode, frames</e>
//
// mode:  1 = invert everything except the hero that used it and what is connected to
//            it (its shadow, summons, clones, puppets, mons, projectiles and effects)
//        2 = invert only that hero and what is connected to it
//        3 = invert everything
//        4 = invert everything except the characters (flogs, heroes, their summons and
//            their shadows)
// The HUD is never inverted. See InvertCompositor.hpp for how each mode is drawn. The "2" versions last a number of the animation's
// frames, or until the animation has finished when the number is left out (like
// setVeil2/setZoom2).
//
// The Circle versions only invert what is inside a circle that starts as a point on
// the hero, grows until it covers the whole screen (everything is inverted by then),
// holds, and shrinks back to nothing, exactly like setRoundVeil.
//
// This class is only the timeline. GameLayer advances it every frame and draws the
// result (see GameLayer::applyInvert). One shared effect: a second one replaces the
// first.
class ScreenInvert
{
public:
	static constexpr float kRoundSeconds = 0.3f; // how long the circle takes to grow, and to shrink

	enum Mode { Others = 1, Self = 2, All = 3, ExceptCharacters = 4 };

	// Returns a number that identifies this effect (0 if it was ignored): release()
	// only acts on the effect it was given the number of.
	int trigger(int mode, float seconds, bool round)
	{
		if (!validMode(mode) || seconds <= 0.0f)
			return 0;

		start(mode, round);
		_held = false;
		_duration = seconds;
		_fade = round ? (std::min)(kRoundSeconds, seconds * 0.5f) : 0.0f;
		return ++_serial;
	}

	// Lasts until it is released (see the "2" tags). `failsafeSeconds` ends it anyway
	// if that never happens.
	int triggerHeld(int mode, float failsafeSeconds, bool round)
	{
		if (!validMode(mode) || failsafeSeconds <= 0.0f)
			return 0;

		start(mode, round);
		_held = true;
		_failsafe = failsafeSeconds;
		_duration = 0.0f;
		_fade = round ? kRoundSeconds : 0.0f;
		return ++_serial;
	}

	// The held effect `id` is over (a circle shrinks away first).
	void release(int id)
	{
		if (!_active || !_held || id != _serial)
			return;
		endHold();
	}

	void update(float dt)
	{
		if (!_active)
			return;

		_elapsed += dt;
		if (_held && _elapsed >= _failsafe)
			endHold();
		if (!_held && _elapsed >= _duration)
			_active = false;
	}

	bool isActive() const { return _active; }
	bool isRound() const { return _round; }
	int mode() const { return _mode; }

	// How much of the full radius (the one that covers the whole screen) the circle
	// has right now, 0 to 1, eased so it speeds up and slows down smoothly. A plain
	// (not round) effect has no circle: this is 1 while it is on.
	float radiusFraction() const
	{
		if (!_active)
			return 0.0f;
		if (!_round)
			return 1.0f;

		float p = _fade > 0.0f ? (std::min)(_elapsed / _fade, 1.0f) : 1.0f;
		if (!_held && _fade > 0.0f)
			p = (std::min)(p, (_duration - _elapsed) / _fade);
		p = (std::max)(p, 0.0f);
		return p * p * (3.0f - 2.0f * p);
	}

private:
	static bool validMode(int mode) { return mode >= Others && mode <= ExceptCharacters; }

	void start(int mode, bool round)
	{
		_mode = mode;
		_round = round;
		_elapsed = 0.0f;
		_active = true;
	}

	// A held effect turns into a timed one that has exactly one fade left.
	void endHold()
	{
		_held = false;
		_duration = _elapsed + _fade;
	}

	int _mode = All;
	float _elapsed = 0.0f;
	float _duration = 0.0f;
	float _failsafe = 0.0f;
	float _fade = 0.0f;
	bool _held = false;
	bool _active = false;
	bool _round = false;
	int _serial = 0;
};
