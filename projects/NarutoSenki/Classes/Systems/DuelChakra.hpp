#pragma once
#include <algorithm>
#include <cstdint>

// Numbers for the shared chakra pool used by Duels and Arcade.
//
// In the arena each side has one pool instead of every hero collecting its own
// CKR/CKR2. The pool is shown as bars; skill 4 spends one bar, skill 5 spends
// three. Holding the ramen button (or the AI standing idle) charges it along a
// ramp that starts slow and ends fast, so a full pool always takes the same
// time no matter how the ramp is tuned.
namespace DuelChakra
{
	constexpr uint32_t kBarSize = 15000;
	constexpr uint32_t kMaxBars = 7;
	constexpr uint32_t kMax = kBarSize * kMaxBars;

	constexpr uint32_t kSkill4Cost = kBarSize;
	constexpr uint32_t kSkill5Cost = kBarSize * 3;

	// The two numbers that define the feel of a charge.
	constexpr float kChargeStartRate = 5000.0f; // units per second at the start of a hold
	constexpr float kChargeSeconds = 3.8f;      // a hold from empty to full

	// rate(t) = start + accel * t, and the pool reaches kMax exactly at
	// kChargeSeconds. Integrating the ramp gives
	//     kMax = start * T + accel * T^2 / 2
	// which is solved for accel here instead of hard-coding the result.
	constexpr float kChargeAccel =
		2.0f * (static_cast<float>(kMax) - kChargeStartRate * kChargeSeconds) / (kChargeSeconds * kChargeSeconds);

	// Units gained during a tick of `dt` seconds after `heldSeconds` of holding.
	// Always at least 1 so a very short tick never stalls the charge.
	inline uint32_t chargeGain(float heldSeconds, float dt)
	{
		const float t = (std::min)(heldSeconds, kChargeSeconds);
		const float rate = kChargeStartRate + kChargeAccel * t;
		const uint32_t gain = static_cast<uint32_t>(rate * dt + 0.5f);
		return gain > 0 ? gain : 1;
	}

	// Chakra earned from landing or taking hits is a tenth of the normal
	// amount in the arena (never below 1), because there is only one target
	// to hit and nothing else to spend the time on.
	inline uint32_t combatGain(uint32_t normalGain)
	{
		return (std::max)(1u, normalGain / 10);
	}

	inline uint32_t wholeBars(uint32_t pool) { return pool / kBarSize; }
	inline float percentIntoBar(uint32_t pool) { return (pool % kBarSize) * 100.0f / kBarSize; }
}
