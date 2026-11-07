#pragma once
#include <algorithm>
#include <cmath>

// Camera zoom on the human's hero, driven by the character XML tag
//
//     <e type="setZoom">1.25, 2</e>
//
// "1.25" is the zoom (1.25x) and "2" is how many seconds pass, counted from the
// moment the tag runs, before the camera starts returning to normal. Both the
// zoom in and the return are tweened (eased), so there is never a jump.
//
// This class is only the timeline. It knows nothing about the camera itself:
// GameLayer asks it how far along the zoom is (weight()) and what scale to use
// (scale()), and BattleRuntimeSystem::updateViewPoint places the camera from that.
// Nothing here touches the duel arena's walls, which are defined in map space.
class CameraZoom
{
public:
	static constexpr float kTweenSeconds = 0.35f; // each way, in and back out
	static constexpr float kMinFactor = 0.5f;
	static constexpr float kMaxFactor = 4.0f;

	// Starts a zoom, or retargets one that is already running. A running zoom keeps
	// the scale it has reached and eases on from there, so triggering again never
	// makes the picture jump.
	// Returns a number that identifies this zoom: release() only acts on the zoom it
	// was given the number of, so a late release from an older animation cannot cut
	// a newer zoom short.
	int trigger(float factor, float holdSeconds)
	{
		factor = (std::min)((std::max)(factor, kMinFactor), kMaxFactor);
		holdSeconds = (std::max)(holdSeconds, 0.0f);

		if (_active)
		{
			const float reached = scale();
			const float span = factor - 1.0f;
			const float weight = std::fabs(span) > 1e-4f ? (reached - 1.0f) / span : 0.0f;
			_progress = inverseSmooth((std::min)((std::max)(weight, 0.0f), 1.0f));
		}
		else
		{
			_progress = 0.0f;
		}

		_peak = factor;
		_holdLeft = holdSeconds;
		_active = true;
		return ++_serial;
	}

	// For a zoom that lasts until something says it is over (a number of animation
	// frames, see the setZoom2 tag). `failsafeSeconds` ends it anyway if that never
	// happens, for example because the animation was interrupted by a hit.
	int triggerHeld(float factor, float failsafeSeconds) { return trigger(factor, failsafeSeconds); }

	// The held zoom `id` is over: start returning to normal.
	void release(int id)
	{
		if (_active && id == _serial)
			_holdLeft = 0.0f;
	}

	void update(float dt)
	{
		if (!_active)
			return;

		const bool holding = _holdLeft > 0.0f;
		if (holding)
			_holdLeft -= dt;

		const float step = dt / kTweenSeconds;
		_progress = holding ? (std::min)(1.0f, _progress + step) : (std::max)(0.0f, _progress - step);

		if (!holding && _progress <= 0.0f)
			_active = false; // back to normal
	}

	bool isActive() const { return _active; }

	// 0 = camera as it normally is, 1 = fully zoomed (eased).
	float weight() const { return smooth(_progress); }

	// The scale to draw the world at: 1 when not zoomed.
	float scale() const { return _active ? 1.0f + (_peak - 1.0f) * weight() : 1.0f; }

	void reset()
	{
		_active = false;
		_progress = 0.0f;
		_holdLeft = 0.0f;
	}

private:
	// Smoothstep, and its inverse (used to keep the picture steady on a retarget).
	static float smooth(float x) { return x * x * (3.0f - 2.0f * x); }
	static float inverseSmooth(float y) { return 0.5f - std::sin(std::asin(1.0f - 2.0f * y) / 3.0f); }

	bool _active = false;
	float _peak = 1.0f;
	float _progress = 0.0f; // 0..1, moves linearly; weight() eases it
	float _holdLeft = 0.0f;
	int _serial = 0;
};
