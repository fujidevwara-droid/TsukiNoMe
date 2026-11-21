#pragma once
#include "Hero.hpp"

/**
 * TobiB
 *
 * Two skill-change buffs (cBuff), both come from the XML:
 *
 *  - 17: Tobi's phased form (skill3 when there is no Kamui dimension to toggle). Faster,
 *        walks with the skill08 frames (skillSPC5Array) and lets hits pass through
 *        while idle / walking / basic attacking. Same as Tobi, nothing else changes.
 *
 *  - 18: the Kamui dimension form (the trap map's own buff, the last field of setBanishMap).
 *        Skills are replaced, everything else (idle, walk, nAttack, skill3, the ougis
 *        buttons' lock state, ...) stays as it is:
 *
 *            skill1 <- skill09 (skillSPC4)       icon TobiB_skill1_1.png
 *            skill2 <- skill10 (skillSPC1)       icon TobiB_skill2_1.png
 *            skill4 <- skill11 (skillSPC2)       icon TobiB_skill4_1.png
 *            skill5 <- skill12 (skillSPC3)       icon TobiB_skill5_1.png
 *
 *        Each replacement brings its own <data> (type, value, ranges, cooldown) along with
 *        the animation. Skill3 is untouched and never locked.
 */
class TobiB : public Hero
{
	void perform() override
	{
		_mainTarget = nullptr;
		findHeroHalf();

		tryBuyGear(GearType::Gear03, GearType::Gear02, GearType::Gear05);

		if (needBackToTowerToRestoreHP() ||
			needBackToDefendTower())
			return;

		if (_mainTarget && _mainTarget->isNotFlog())
		{
			Vec2 moveDirection;
			Vec2 sp = getDistanceToTarget();

			if (isFreeState())
			{
				if (_isCanOugis2 && !_isControlled && getGameLayer()->_isOugis2Game && _mainTarget->getDEF() < 5000 && !_mainTarget->_isArmored && _mainTarget->getState() != State::KNOCKDOWN && !_mainTarget->_isSticking)
				{
					if (abs(sp.x) > 48 || abs(sp.y) > 16)
					{
						moveDirection = sp.getNormalized();
						walk(moveDirection);
						return;
					}

					changeSide(sp);
					attack(OUGIS2);
					return;
				}
				else if (_isCanOugis1 && !_isControlled && _mainTarget->getDEF() < 5000)
				{
					changeSide(sp);
					attack(OUGIS1);
					return;
				}
				else if (_isCanSkill3 && !_skillChangeBuffValue)
				{
					changeSide(sp);
					attack(SKILL3);
					return;
				}
				else if (enemyCombatPoint > friendCombatPoint && abs(enemyCombatPoint - friendCombatPoint) > 3000 && !_isHealing && !_isControlled && !_skillChangeBuffValue)
				{
					if (abs(sp.x) < 160)
						stepBack2();
					else
						idle();
					return;
				}
				else if (abs(sp.x) < 128)
				{
					if (abs(sp.x) > 64 || abs(sp.y) > 32)
					{
						moveDirection = sp.getNormalized();
						walk(moveDirection);
						return;
					}
					else if (abs(sp.x) < 32 && (_isCanSkill2 && _mainTarget->getHpPercent() < 0.5f))
					{
						stepBack();
						return;
					}

					// Phased (17) keeps the old Tobi rule: only skills on a weak target.
					// In the dimension (18) the replaced skills are used freely.
					if (_isCanSkill2 && _mainTarget->getDEF() < 5000 && (_mainTarget->getHpPercent() < 0.5f || _skillChangeBuffValue != 17))
					{
						changeSide(sp);
						attack(SKILL2);
					}
					else if (_isCanSkill1 && _mainTarget->getDEF() < 5000 && (_mainTarget->getHpPercent() < 0.5f || _skillChangeBuffValue != 17))
					{
						changeSide(sp);
						attack(SKILL1);
					}
					else
					{
						if (abs(sp.x) > 32 || abs(sp.y) > 32)
						{
							moveDirection = sp.getNormalized();
							walk(moveDirection);
							return;
						}

						if (_isCanGear03)
						{
							useGear(GearType::Gear03);
						}

						changeSide(sp);
						attack(NAttack);
					}

					return;
				}
			}
		}
		_mainTarget = nullptr;
		if (notFindFlogHalf())
			findTowerHalf();

		if (_mainTarget)
		{
			Vec2 moveDirection;
			Vec2 sp = getDistanceToTarget();

			if (abs(sp.x) > 32 || abs(sp.y) > 32)
			{
				moveDirection = sp.getNormalized();
				walk(moveDirection);
				return;
			}

			if (isFreeState())
			{
				if (_isCanSkill2 && _mainTarget->isFlog() && isBaseDanger)
				{
					changeSide(sp);
					attack(SKILL2);
				}
				else if (_isCanSkill1 && _mainTarget->isFlog())
				{
					changeSide(sp);
					attack(SKILL1);
				}
				else
				{
					if (_isCanGear03)
					{
						useGear(GearType::Gear03);
					}
					changeSide(sp);
					attack(NAttack);
				}
			}

			return;
		}

		checkHealingState();
	}

	// Runs whenever a cBuff starts (applyBuff) or is taken away (endCBuff). By then
	// _skillChangeBuffValue already holds the new value, 0 meaning "no buff": nothing to
	// do, resumeAction() has put everything back.
	void changeAction() override
	{
		if (_skillChangeBuffValue == 17)
		{
			revertSkillSwap(); // 18 -> 17: one form at a time
			applyPhase();
		}
		else if (_skillChangeBuffValue == 18)
		{
			revertPhase(); // 17 -> 18
			applySkillSwap();
		}
	}

	void resumeAction(float dt) override
	{
		revertPhase();
		revertSkillSwap();
		CharacterBase::resumeAction(dt);
	}

	void setActionResume() override
	{
		unschedule(schedule_selector(TobiB::resumeAction));

		revertPhase();
		revertSkillSwap();

		_skillChangeBuffValue = 0;
	}

	// A dead TobiB never keeps a replaced skill set (or its icons) into the next life.
	void dead() override
	{
		revertPhase();
		revertSkillSwap();
		Hero::dead();
	}

	/**
	 * Callbacks
	 */

	bool onAcceptAttack(CharacterBase *attacker) override
	{
		// Only the phased form (17) lets hits through.
		if (_skillChangeBuffValue == 17 &&
			(_state == State::IDLE ||
			 _state == State::WALK ||
			 _state == State::NATTACK))
		{
			if (getOpacity() == 255)
			{
				scheduleOnce(schedule_selector(CharacterBase::disableBuff), 0.2f);
			}

			setOpacity(150);
			return false;
		}

		return true;
	}

	/**
	 * cBuff 17: phased
	 */

	void applyPhase()
	{
		// TobiB's walk frames are skill08 (skillSPC5Array), skill10 is an attack now.
		if (!skillSPC5Array)
			return;

		_isPhaseActive = true;

		setWalkAction(createAnimation(skillSPC5Array, 10, true, false));

		setWalkSpeed(320);
		_originSpeed = 320;

		for (auto hero : getGameLayer()->_CharacterArray)
		{
			if (getGroup() != hero->getGroup() && hero->isPlayerOrCom() && hero->getState() != State::HURT && hero->getState() != State::DEAD)
			{
				float distanceX = (hero->getPosition() - getPosition()).x;
				if (distanceX < kAttackRange)
				{
					if (!hero->_isVisable)
					{
						if (hero->getName() == HeroEnum::Konan ||
							hero->getName() == HeroEnum::Deidara)
						{
							hero->unschedule(schedule_selector(CharacterBase::disableBuff));
						}

						hero->setOpacity(255);
						hero->setVisible(true);

						if (hero->_hpBar)
							hero->_hpBar->setVisible(true);
						if (hero->_shadow)
							hero->_shadow->setVisible(true);

						hero->_isVisable = true;
					}
				}
			}
		}
	}

	void revertPhase()
	{
		if (!_isPhaseActive)
			return;

		_isPhaseActive = false;

		setWalkAction(createAnimation(walkArray, 10, true, false));

		if (getOpacity() != 255)
			setOpacity(255);

		setWalkSpeed(224);
		_originSpeed = 224;

		if (_state == State::WALK)
			idle();
	}

	/**
	 * cBuff 18: skill1/2/4/5 replaced by skill09/10/11/12
	 */

	void applySkillSwap()
	{
		if (_isSwapActive)
			return;

		// Every replacement needs its animation; without the whole set nothing is swapped
		// (a half replaced kit is worse than none).
		if (!skillSPC4Array || !skillSPC1Array || !skillSPC2Array || !skillSPC3Array)
			return;

		_isSwapActive = true;

		// --- Cache the base kit ---
		_base1.action = getSkill1Action();
		_base1.type = getSAttackType1();
		_base1.value = getSAttackValue1();
		_base1.rangeX = _sAttackRangeX1;
		_base1.rangeY = _sAttackRangeY1;

		_base2.action = getSkill2Action();
		_base2.type = getSAttackType2();
		_base2.value = getSAttackValue2();
		_base2.rangeX = _sAttackRangeX2;
		_base2.rangeY = _sAttackRangeY2;

		_base4.action = getSkill4Action();
		_base4.type = getSAttackType4();
		_base4.value = getSAttackValue4();
		_base4.rangeX = _sAttackRangeX4;
		_base4.rangeY = _sAttackRangeY4;

		_base5.action = getSkill5Action();
		_base5.type = getSAttackType5();
		_base5.value = getSAttackValue5();
		_base5.rangeX = _sAttackRangeX5;
		_base5.rangeY = _sAttackRangeY5;

		// --- Skill1 -> skill09 (SPC4) ---
		setSkill1Action(createAnimation(skillSPC4Array, 10, false, true));
		setSAttackType1(getSpcAttack4Type());
		setSAttackValue1(getSpcAttackValue4());
		_sAttackRangeX1 = _spcAttackRangeX4;
		_sAttackRangeY1 = _spcAttackRangeY4;
		_base1.cdDelta = (int)_spcAttackCD4 - (int)_sAttackCD1;
		_sAttackCD1 = _spcAttackCD4;

		// --- Skill2 -> skill10 (SPC1) ---
		setSkill2Action(createAnimation(skillSPC1Array, 10, false, true));
		setSAttackType2(getSpcAttack1Type());
		setSAttackValue2(getSpcAttackValue1());
		_sAttackRangeX2 = _spcAttackRangeX1;
		_sAttackRangeY2 = _spcAttackRangeY1;
		_base2.cdDelta = (int)_spcAttackCD1 - (int)_sAttackCD2;
		_sAttackCD2 = _spcAttackCD1;

		// --- Skill4 (ougis 1) -> skill11 (SPC2) ---
		setSkill4Action(createAnimation(skillSPC2Array, 10, false, true));
		setSAttackType4(getSpcAttack2Type());
		setSAttackValue4(getSpcAttackValue2());
		_sAttackRangeX4 = _spcAttackRangeX2;
		_sAttackRangeY4 = _spcAttackRangeY2;

		// --- Skill5 (ougis 2) -> skill12 (SPC3) ---
		setSkill5Action(createAnimation(skillSPC3Array, 10, false, true));
		setSAttackType5(getSpcAttack3Type());
		setSAttackValue5(getSpcAttackValue3());
		_sAttackRangeX5 = _spcAttackRangeX3;
		_sAttackRangeY5 = _spcAttackRangeY3;

		// Skill3, idle, walk, nAttack, the ougis buttons' lock: not touched.

		updateSkillIcons(true);
		refreshSkillCooldownButtons();
	}

	void revertSkillSwap()
	{
		if (!_isSwapActive)
			return;

		_isSwapActive = false;

		if (_base1.action)
			setSkill1Action(_base1.action);
		setSAttackType1(_base1.type);
		setSAttackValue1(_base1.value);
		_sAttackRangeX1 = _base1.rangeX;
		_sAttackRangeY1 = _base1.rangeY;
		_sAttackCD1 = (uint32_t)((int)_sAttackCD1 - _base1.cdDelta);

		if (_base2.action)
			setSkill2Action(_base2.action);
		setSAttackType2(_base2.type);
		setSAttackValue2(_base2.value);
		_sAttackRangeX2 = _base2.rangeX;
		_sAttackRangeY2 = _base2.rangeY;
		_sAttackCD2 = (uint32_t)((int)_sAttackCD2 - _base2.cdDelta);

		if (_base4.action)
			setSkill4Action(_base4.action);
		setSAttackType4(_base4.type);
		setSAttackValue4(_base4.value);
		_sAttackRangeX4 = _base4.rangeX;
		_sAttackRangeY4 = _base4.rangeY;

		if (_base5.action)
			setSkill5Action(_base5.action);
		setSAttackType5(_base5.type);
		setSAttackValue5(_base5.value);
		_sAttackRangeX5 = _base5.rangeX;
		_sAttackRangeY5 = _base5.rangeY;

		updateSkillIcons(false);
		refreshSkillCooldownButtons();
	}

	// swapped = true: TobiB_skillN_1.png, false: TobiB_skillN.png (N = 1, 2, 4, 5)
	void updateSkillIcons(bool swapped)
	{
		if (!isPlayer())
			return;

		auto hud = getGameLayer()->getHudLayer();
		const char *suffix = swapped ? "_1" : "";

		auto set = [&](ActionButton *button, int skillNum)
		{
			if (!button)
				return;
			auto frame = getSpriteFrame(format("TobiB_skill{}{}.png", skillNum, suffix));
			if (frame)
				button->setDisplayFrame(frame);
		};

		set(hud->skill1Button, 1);
		set(hud->skill2Button, 2);
		set(hud->skill4Button, 4);
		set(hud->skill5Button, 5);
	}

	// The buttons count their own cooldown: tell them about the new skill1 / skill2 one.
	void refreshSkillCooldownButtons()
	{
		if (!isPlayer())
			return;

		auto hud = getGameLayer()->getHudLayer();
		if (hud->skill1Button)
			hud->skill1Button->setCD(_sAttackCD1 * 1000);
		if (hud->skill2Button)
			hud->skill2Button->setCD(_sAttackCD2 * 1000);
	}

private:
	bool _isPhaseActive = false; // cBuff 17 is on
	bool _isSwapActive = false;	 // cBuff 18 is on

	// What a replaced skill slot was before the swap.
	struct SkillSlot
	{
		RefPtr<FiniteTimeAction> action;
		string type;
		uint32_t value = 0;
		int rangeX = 0;
		int rangeY = 0;
		int cdDelta = 0; // new cooldown - old one, taken back on revert (gear may have changed it meanwhile)
	};

	SkillSlot _base1;
	SkillSlot _base2;
	SkillSlot _base4;
	SkillSlot _base5;
};
