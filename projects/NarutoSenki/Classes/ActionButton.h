#pragma once
#include "Defines.h"

class HudLayer;
class CharacterBase;

class ActionButton : public Sprite, public CCTouchDelegate
{
public:
	ActionButton();

	int _clickNum;
	float _clickTime;
	ProgressTimer *markSprite;
	Sprite *ougismarkSprite;
	Sprite *proressmarkSprite;
	Sprite *proressblinkSprite;
	Sprite *progressPointSprite;
	Sprite *proressblinkMask;
	const char *_cost;
	ClippingNode *clipper;
	bool _isLock;

	bool _isColdChanged;

	bool init(const string &szImage);
	bool isCanClick();
	CCRect getRect();

	void click();
	void setMarkSprite(const char *mark);
	void setOugisMark();
	void setProgressMark();
	void updateProgressMark();
	void reset();

	Sprite *gearSign;
	CCLabelBMFont *cdLabel;

	CCLabelBMFont *lockLabel1;
	CCLabelBMFont *lockLabel2;

	bool _isMarkVisable;

	PROP(ABType, _abType, ABType);
	GearType _gearType;

	PROP_UInt(_cooldown, CD);
	PROP(bool, _isDoubleSkill, DoubleSkill);
	PROP_UInt(_timeCount, TimeCount);

	// showX: also draw an "X" over the button (used for a fallen Duels hero).
	void setLock(bool showX = false);
	void unLock();

	// Raises the cooldown to at least minMs (never shortens a longer one) and
	// makes sure it counts back down. Setting the time count directly would
	// leave it stuck, because only a click ever schedules the countdown.
	void applyMinCooldown(uint32_t minMs);

	// Arena only: holding the ramen button charges the shared chakra pool
	// (see Systems/DuelChakra.hpp). The press is consumed, nothing is healed.
	void startChakraCharge();
	void stopChakraCharge();
	void updateChakraCharge(float dt);

	bool _isChargingChakra = false;
	CharacterBase *_chargingHero = nullptr; // retained while a charge is running
	float _chakraChargeHeld = 0.0f; // seconds held in this press
	void clearOugisMark();
	void setGearType(GearType type);

	PROP_PTR(Action, _freezeAction, FreezeAction);
	PROP(HudLayer *, _delegate, Delegate);
	void beganAnimation(bool isLock = false);
	void updateCDLabel(float dt);

	static ActionButton *create(const string &szImage);

protected:
	void onEnter();
	void onExit();
	bool ccTouchBegan(Touch *touch, Event *event);
	void ccTouchEnded(Touch *touch, Event *event);
	void ccTouchCancelled(Touch *touch, Event *event);

	void createFreezeAnimation();
	void clearClick();

	bool containsTouchLocation(Touch *touch);
};
