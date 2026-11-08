#pragma once
#include <algorithm>

// Screen veil (a colour laid over the picture), driven by the character XML tags
//
//     <e type="setVeil">r, g, b, a, seconds</e>
//     <e type="setVeil2">r, g, b, a, frames</e>
//
// r, g, b and a are 0-255 (a is the opacity: 0 is invisible, 255 is solid), e.g.
// "110, 0, 0, 120" is a dark, fairly see-through red. setVeil lasts a number of
// seconds. setVeil2 lasts a number of the animation's frames, or until the
// animation has finished when the number is left out (like setZoom2/setShake2).
//
// The veil fades in and out over a very short time so it never pops on or off.
//
// The round versions (see below) use the same timeline, but instead of fading
// they grow: a circle that starts as a point and expands until it has covered the
// whole screen, holds, then shrinks back down to nothing.
//
//     <e type="setRoundVeil">r, g, b, a, seconds</e>
//     <e type="setRoundVeil2">r, g, b, a, frames</e>
//
// This class is only the timeline. GameLayer advances it every frame and
// shows the result on a colour layer between the game world and the HUD. One
// shared veil, like ScreenShake: a second veil simply replaces the first.
class ScreenVeil
{
public:
	static constexpr float kFadeSeconds = 0.08f;
	static constexpr float kRoundSeconds = 0.3f; // how long the circle takes to grow, and to shrink again

	// Returns a number that identifies this veil (0 if it was ignored): release()
	// only acts on the veil it was given the number of.
	int trigger(int r, int g, int b, int a, float seconds, bool round = false)
	{
		seconds = (std::max)(seconds, 0.0f);
		a = clamp255(a);
		if (a <= 0 || seconds <= 0.0f)
			return 0;

		start(r, g, b, a, round);
		_held = false;
		_duration = seconds;
		_fade = (std::min)(round ? kRoundSeconds : kFadeSeconds, seconds * 0.5f);
		return ++_serial;
	}

	// A veil that lasts until it is released (see the setVeil2 tag).
	// `failsafeSeconds` ends it anyway if that never happens.
	int triggerHeld(int r, int g, int b, int a, float failsafeSeconds, bool round = false)
	{
		a = clamp255(a);
		if (a <= 0 || failsafeSeconds <= 0.0f)
			return 0;

		start(r, g, b, a, round);
		_held = true;
		_failsafe = failsafeSeconds;
		_duration = 0.0f;
		_fade = round ? kRoundSeconds : kFadeSeconds;
		return ++_serial;
	}

	// The held veil `id` is over: it fades out.
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

	// 0 to 1: how far in the veil is right now (it rises while the veil comes in,
	// stays at 1 while it holds and falls while it goes out).
	float progress() const
	{
		if (!_active)
			return 0.0f;

		float factor = _fade > 0.0f ? (std::min)(_elapsed / _fade, 1.0f) : 1.0f;
		if (!_held && _fade > 0.0f)
			factor = (std::min)(factor, (_duration - _elapsed) / _fade);
		return (std::max)(factor, 0.0f);
	}

	// 0-255. A plain veil fades with progress(); a round one keeps its opacity and
	// shows progress() as its size instead (see radiusFraction()).
	float opacity() const
	{
		if (!_active)
			return 0.0f;
		return _round ? _alpha : _alpha * progress();
	}

	// How much of the full radius (the one that covers the whole screen) the circle
	// has right now, 0 to 1, eased so it speeds up and slows down smoothly.
	float radiusFraction() const
	{
		const float p = progress();
		return p * p * (3.0f - 2.0f * p);
	}

	int red() const { return _r; }
	int green() const { return _g; }
	int blue() const { return _b; }

private:
	static int clamp255(int v) { return (std::min)((std::max)(v, 0), 255); }

	void start(int r, int g, int b, int a, bool round)
	{
		_round = round;
		_r = clamp255(r);
		_g = clamp255(g);
		_b = clamp255(b);
		_alpha = static_cast<float>(a);
		_elapsed = 0.0f;
		_active = true;
	}

	// A held veil turns into a timed one that has exactly one fade left.
	void endHold()
	{
		_held = false;
		_duration = _elapsed + _fade;
	}

	int _r = 0;
	int _g = 0;
	int _b = 0;
	float _alpha = 0.0f;
	float _elapsed = 0.0f;
	float _duration = 0.0f;
	float _failsafe = 0.0f;
	float _fade = kFadeSeconds;
	bool _held = false;
	bool _active = false;
	bool _round = false;
	int _serial = 0;
};
