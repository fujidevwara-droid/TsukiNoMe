#pragma once
#include <algorithm>

// Timeline of the spiral transition (transition style 2 of setMapChange / setTrapMap): the
// picture twists into a spiral and sinks into darkness, holds there, and comes back the same
// way. The swap behind it happens while it is fully dark; the systems that use it time that
// themselves (kInSeconds), so this class only says how far in the effect is.
//
// GameLayer::applySwirl draws it: the world (background and game layer, not the HUD) is drawn
// into a texture every frame and shown through the swirl shader (Systems/SwirlShader.hpp).
class ScreenSwirl
{
public:
	static constexpr float kInSeconds = 0.6f;	// into the spiral and the dark
	static constexpr float kHoldSeconds = 0.1f; // fully dark
	static constexpr float kOutSeconds = 0.6f;	// and back out of it

	// How hard the picture is twisted and pulled in at full strength.
	static constexpr float kMaxTwist = 8.0f; // radians at the centre: more than a turn
	static constexpr float kMaxPull = 1.2f;

	// A time of 0 skips that part (an "in" of 0 starts fully in: a reveal).
	void start(float inSeconds, float holdSeconds, float outSeconds)
	{
		_in = (std::max)(inSeconds, 0.0f);
		_hold = (std::max)(holdSeconds, 0.0f);
		_out = (std::max)(outSeconds, 0.0f);
		_elapsed = 0.0f;
		_active = (_in + _hold + _out) > 0.0f;
	}

	void update(float dt)
	{
		if (!_active)
			return;

		_elapsed += dt;
		if (_elapsed >= _in + _hold + _out)
			_active = false;
	}

	bool isActive() const { return _active; }

	// 0 (nothing) to 1 (fully twisted and dark), eased on the way in and out.
	float amount() const
	{
		if (!_active)
			return 0.0f;

		float p;
		if (_elapsed < _in)
			p = _elapsed / _in;
		else if (_elapsed < _in + _hold)
			p = 1.0f;
		else
			p = _out > 0.0f ? 1.0f - (_elapsed - _in - _hold) / _out : 0.0f;

		p = (std::min)((std::max)(p, 0.0f), 1.0f);
		return p * p * (3.0f - 2.0f * p);
	}

private:
	float _in = 0.0f;
	float _hold = 0.0f;
	float _out = 0.0f;
	float _elapsed = 0.0f;
	bool _active = false;
};
