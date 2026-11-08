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

	// Starts a zoom. If one is already running (or still returning to normal), the new
	// one simply overrides it: the picture eases from whatever scale it is at right
	// now to the new factor, so there is never a jump. E.g. at 1.5x, triggering 1.25
	// eases from 1.5x down to 1.25x (of the normal view), it does not snap.
	// Returns a number that identifies this zoom: release() only acts on the zoom it
	// was given the number of, so a late release from an older animation cannot cut
	// a newer zoom short.
	int trigger(float factor, float holdSeconds)
	{
		factor = (std::min)((std::max)(factor, kMinFactor), kMaxFactor);
		holdSeconds = (std::max)(holdSeconds, 0.0f);

		// Ease on from the scale and camera blend we are at right now (1 and 0 when idle).
		_scaleFrom = scale();
		_weightFrom = weight();
		_scaleTo = factor;
		_weightTo = 1.0f;
		_progress = 0.0f;
		_returning = false;
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
		if (_active && !_returning && id == _serial)
			_holdLeft = 0.0f;
	}

	void update(float dt)
	{
		if (!_active)
			return;

		const float step = dt / kTweenSeconds;

		if (!_returning)
		{
			_holdLeft -= dt;
			_progress = (std::min)(1.0f, _progress + step);
			if (_holdLeft <= 0.0f)
			{
				// Hold is over: ease from where we are back to the normal view.
				_scaleFrom = scale();
				_weightFrom = weight();
				_scaleTo = 1.0f;
				_weightTo = 0.0f;
				_progress = 0.0f;
				_returning = true;
			}
			return;
		}

		_progress = (std::min)(1.0f, _progress + step);
		if (_progress >= 1.0f)
			reset(); // back to normal
	}

	bool isActive() const { return _active; }

	// 0 = camera as it normally is, 1 = camera fully on the zoomed view (eased).
	// Stays at 1 while one zoom factor eases into another.
	float weight() const
	{
		if (!_active)
			return 0.0f;
		const float e = smooth(_progress);
		return _weightFrom + (_weightTo - _weightFrom) * e;
	}

	// The scale to draw the world at: 1 when not zoomed.
	float scale() const
	{
		if (!_active)
			return 1.0f;
		const float e = smooth(_progress);
		return _scaleFrom + (_scaleTo - _scaleFrom) * e;
	}

	void reset()
	{
		_active = false;
		_returning = false;
		_progress = 0.0f;
		_holdLeft = 0.0f;
		_scaleFrom = _scaleTo = 1.0f;
		_weightFrom = _weightTo = 0.0f;
	}

private:
	// Smoothstep.
	static float smooth(float x) { return x * x * (3.0f - 2.0f * x); }

	bool _active = false;
	bool _returning = false;
	float _scaleFrom = 1.0f, _scaleTo = 1.0f;
	float _weightFrom = 0.0f, _weightTo = 0.0f;
	float _progress = 0.0f; // 0..1, moves linearly; smooth() eases it
	float _holdLeft = 0.0f;
	int _serial = 0;
};
