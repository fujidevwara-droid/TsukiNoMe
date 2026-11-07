#pragma once
#include <algorithm>
#include <cstdlib>

// Screen shake, driven by the character XML tag
//
//     <e type="setShake">8, 0.4</e>
//
// "8" is the strength (the largest the picture is knocked off by, in pixels) and
// "0.4" is how long the shake lasts, in seconds. It is strongest at the start
// and eases out to nothing.
//
// This class is only the timeline. GameLayer samples it every frame and moves the
// scene by the result (and puts the scene back when it ends). It is one shared
// shake rather than an action on the scene because an action remembers where the
// scene was when it started and restores that when it ends: two of them running
// together would leave the screen stuck off-centre. Here a second shake just
// replaces or is ignored by the first.
class ScreenShake
{
public:
	static constexpr float kMaxStrength = 40.0f; // pixels
	static constexpr float kReleaseTailSeconds = 0.12f; // how long a released held shake takes to die away

	// Returns a number that identifies this shake (0 if it was ignored): release()
	// only acts on the shake it was given the number of.
	int trigger(float strength, float seconds)
	{
		strength = (std::min)((std::max)(strength, 0.0f), kMaxStrength);
		seconds = (std::max)(seconds, 0.0f);
		if (strength <= 0.0f || seconds <= 0.0f)
			return 0;

		// A shake that is already running is only replaced by one at least as strong
		// as what is left of it, so a light tremor cannot cut a heavy one short.
		if (_left > 0.0f && strength < amplitude())
			return 0;

		_strength = strength;
		_duration = seconds;
		_left = seconds;
		_held = false;
		return ++_serial;
	}

	// A shake at full strength that lasts until it is released (see the setShake2
	// tag). `failsafeSeconds` ends it anyway if that never happens.
	int triggerHeld(float strength, float failsafeSeconds)
	{
		const int id = trigger(strength, failsafeSeconds);
		if (id != 0)
			_held = true;
		return id;
	}

	// The held shake `id` is over: it eases out over a short tail.
	void release(int id)
	{
		if (!_held || id != _serial)
			return;
		_held = false;
		_duration = kReleaseTailSeconds;
		_left = (std::min)(_left, kReleaseTailSeconds);
	}

	void update(float dt)
	{
		if (_left > 0.0f)
			_left = (std::max)(0.0f, _left - dt);
		if (_left <= 0.0f)
			_held = false;
	}

	bool isActive() const { return _left > 0.0f; }

	// How far the picture can be knocked off right now.
	float amplitude() const
	{
		if (_held)
			return _strength;
		return _duration > 0.0f ? _strength * (_left / _duration) : 0.0f;
	}

	// A random offset within the current amplitude.
	void sample(float &x, float &y) const
	{
		const float a = amplitude();
		x = (random01() * 2.0f - 1.0f) * a;
		y = (random01() * 2.0f - 1.0f) * a;
	}

private:
	static float random01() { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); }

	float _strength = 0.0f;
	float _duration = 0.0f;
	float _left = 0.0f;
	bool _held = false;
	int _serial = 0;
};
