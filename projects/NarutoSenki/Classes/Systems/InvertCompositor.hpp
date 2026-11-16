#pragma once
#include "Defines.h"
#include "CharacterBase.h"
#include "Effect.h"
#include "Data/Config.h"
#include "Systems/InvertShader.hpp"
#include <set>

// Decides, every frame while an invert is on (see ScreenInvert.hpp), which things wear
// the inverting shader instead of relying on the screen-wide overlay alone. Modes:
//
//   1  everything except the caster and what is connected to it
//   2  only the caster and what is connected to it
//   3  everything                       (overlay only, nothing for this class to do)
//   4  everything except the characters: flogs, heroes, their summons and their shadows
//
// "Connected to the caster" is the caster, everything whose master (or second master)
// is the caster or something connected to it (clones, kuchiyose, kugutsu, mons,
// projectiles), the effects that belong to any of those, and the shadows of those.
//
// Mode 1 and 4 run on top of the overlay, which inverts the whole picture: whatever
// should stay normal is inverted once more by its own shader, so the two cancel
// exactly. Mode 2 inverts only what wears the shader.
//
// Units, projectiles and most effects are ordinary sprites and just get the shader.
// Shadows and the effects in the skill/damage effect batches are drawn by shared
// SpriteBatchNodes, whose sprites cannot have a shader of their own. So the ones
// that belong to the caster are moved into a twin batch (same texture, same draw
// order, wearing the shader) for as long as the invert lasts, then moved back.
class InvertCompositor
{
public:
	struct Batches
	{
		SpriteBatchNode *skillEffect = nullptr;
		SpriteBatchNode *damageEffect = nullptr;
		SpriteBatchNode *shadow = nullptr;
	};

	~InvertCompositor()
	{
		// Teardown only lets go of what it holds; the nodes are about to go anyway.
		for (auto node : _shaded)
			node->release();
	}

	// Call every frame while mode 1, 2 or 4 is on. `world` is the node the units and the
	// batches are children of (the GameLayer).
	void update(Node *world, const Batches &batches, CharacterBase *caster, int mode)
	{
		vector<CharacterBase *> units;
		CCObject *obj = nullptr;
		CCARRAY_FOREACH(world->getChildren(), obj)
		{
			if (auto unit = dynamic_cast<CharacterBase *>(obj))
				units.push_back(unit);
		}

		const bool followsCaster = mode == 1 || mode == 2;
		std::set<CharacterBase *> connected;
		std::set<CCNode *> wanted;

		if (followsCaster)
		{
			if (caster)
				connected.insert(caster);

			// Owners of owners count too (a projectile of a puppet of the caster): a few
			// passes over pointers that are all alive, nothing is dereferenced that is not.
			for (int pass = 0; pass < 4; ++pass)
			{
				bool grew = false;
				for (auto unit : units)
				{
					if (connected.count(unit))
						continue;
					if (connected.count(unit->getMaster()) || connected.count(unit->getSecMaster()))
					{
						connected.insert(unit);
						grew = true;
					}
				}
				if (!grew)
					break;
			}

			wanted.insert(connected.begin(), connected.end());

			// Effects that sit directly in the world rather than in a batch.
			CCARRAY_FOREACH(world->getChildren(), obj)
			{
				if (auto effect = dynamic_cast<Effect *>(obj))
				{
					if (connected.count(effect->getAttacker()))
						wanted.insert(effect);
				}
			}
		}
		else if (mode == 4)
		{
			for (auto unit : units)
			{
				if (unit->isNotTower() && unit->isNotBullet())
					wanted.insert(unit);
			}
		}

		// Whatever stopped being wanted goes back to normal.
		for (auto it = _shaded.begin(); it != _shaded.end();)
		{
			if (wanted.count(*it))
			{
				++it;
				continue;
			}
			if ((*it)->getParent())
				InvertShader::restore(*it);
			(*it)->release();
			it = _shaded.erase(it);
		}

		// Everything wanted wears the shader (again: effects it spawned meanwhile are
		// picked up). In mode 4 the effects attached to a character stay inverted.
		for (auto node : wanted)
		{
			InvertShader::apply(node, mode == 4);
			if (_shaded.insert(node).second)
				node->retain();
		}

		if (followsCaster)
		{
			moveOwnedEffects(world, batches.skillEffect, 0, kSkillEffectOrder, connected);
			moveOwnedEffects(world, batches.damageEffect, 1, kDamageEffectOrder, connected);

			// The shadows of the connected units.
			if (batches.shadow)
			{
				SpriteBatchNode *twin = twinFor(world, batches.shadow, 2, kShadowOrder);
				for (auto unit : connected)
				{
					if (unit->_shadow && unit->_shadow->getParent() == batches.shadow)
						moveSprite(unit->_shadow, twin);
				}
			}
		}

		// Mode 4 leaves every shadow alone, which is the whole shadow batch.
		if (batches.shadow)
		{
			if (mode == 4 && !_shadowBatchShaded)
			{
				if (auto prog = InvertShader::program(batches.shadow->getTexture()->hasPremultipliedAlpha()))
					batches.shadow->setShaderProgram(prog);
				_shadowBatchShaded = true;
			}
			else if (mode != 4)
			{
				restoreShadowBatch(batches.shadow);
			}
		}
	}

	// Everything back to normal: the shader off, the twin batches emptied.
	void clear(const Batches &batches)
	{
		for (auto node : _shaded)
		{
			if (node->getParent())
				InvertShader::restore(node);
			node->release();
		}
		_shaded.clear();

		for (int slot = 0; slot < 3; ++slot)
		{
			if (!_twin[slot] || !_home[slot])
				continue;

			vector<Sprite *> sprites;
			CCObject *obj = nullptr;
			CCARRAY_FOREACH(_twin[slot]->getChildren(), obj)
			{
				if (auto sprite = dynamic_cast<Sprite *>(obj))
					sprites.push_back(sprite);
			}
			for (auto sprite : sprites)
				moveSprite(sprite, _home[slot]);
		}

		if (batches.shadow)
			restoreShadowBatch(batches.shadow);
	}

	bool isDirty() const { return !_shaded.empty() || _shadowBatchShaded; }

private:
	SpriteBatchNode *twinFor(Node *world, SpriteBatchNode *home, int slot, int zOrder)
	{
		if (!_twin[slot])
		{
			_twin[slot] = SpriteBatchNode::createWithTexture(home->getTexture());
			_home[slot] = home;
			world->addChild(_twin[slot], zOrder);
		}

		if (auto prog = InvertShader::program(home->getTexture()->hasPremultipliedAlpha()))
			_twin[slot]->setShaderProgram(prog);
		return _twin[slot];
	}

	// The effects in `home` that belong to a connected unit move to its twin.
	void moveOwnedEffects(Node *world, SpriteBatchNode *home, int slot, int zOrder, const std::set<CharacterBase *> &connected)
	{
		if (!home)
			return;

		vector<Sprite *> owned;
		CCObject *obj = nullptr;
		CCARRAY_FOREACH(home->getChildren(), obj)
		{
			if (auto effect = dynamic_cast<Effect *>(obj))
			{
				if (connected.count(effect->getAttacker()))
					owned.push_back(effect);
			}
		}

		if (owned.empty())
			return;

		SpriteBatchNode *twin = twinFor(world, home, slot, zOrder);
		for (auto sprite : owned)
			moveSprite(sprite, twin);
	}

	static void moveSprite(Sprite *sprite, SpriteBatchNode *to)
	{
		if (sprite->getParent() == to)
			return;

		sprite->retain();
		const int zOrder = sprite->getZOrder();
		sprite->removeFromParentAndCleanup(false); // keep its actions running
		to->addChild(sprite, zOrder);
		sprite->release();
	}

	void restoreShadowBatch(SpriteBatchNode *shadowBatch)
	{
		if (!_shadowBatchShaded)
			return;
		if (auto normal = CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor))
			shadowBatch->setShaderProgram(normal);
		_shadowBatchShaded = false;
	}

	std::set<CCNode *> _shaded; // retained: wearing the shader right now
	SpriteBatchNode *_twin[3] = { nullptr, nullptr, nullptr }; // skill effects, damage effects, shadows
	SpriteBatchNode *_home[3] = { nullptr, nullptr, nullptr };
	bool _shadowBatchShaded = false;
};
