#pragma once
#include "Defines.h"

class CharacterBase;

class Effect : public Sprite
{
public:
	bool init(const string &szImage, CharacterBase *attacker);

	static Effect *create(const string &szImage, CharacterBase *attacker);

	// The character this effect belongs to. Only ever compared, never dereferenced: it can
	// outlive the character.
	CharacterBase *getAttacker() const { return at; }

private:
	FiniteTimeAction *createEffectAnimation(const string &file, size_t frameCount, uint8_t fps, bool isLoop);
	FiniteTimeAction *createFontAnimation();
	void removeEffect();
	void removeFontEffect();

	CharacterBase *at = nullptr;
};
