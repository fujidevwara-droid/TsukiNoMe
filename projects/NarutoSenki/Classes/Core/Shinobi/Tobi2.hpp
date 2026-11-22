#pragma once
#include "Hero.hpp"
#include "TobiB.hpp"

// The Arcade boss. Its Phase2 turns it into TobiB with setSwitchChar, but the C++ object stays a
// Tobi2, so from then on every override below hands over to TobiB's own code (AI, skill swap,
// phase, hit handling). Without this the boss kept Tobi2's AI and Tobi2::changeAction, which
// uses skillSPC1Array as its walk frames: with TobiB's data that is skill10, an attack.
class Tobi2 : public TobiB
{
	bool isTobiBForm() { return getName() == HeroEnum::TobiB; }

	// Turned into TobiB: he starts in the normal skill set. The replaced set (cBuff 18,
	// skill1/2/4/5 -> skill09/10/11/12) comes only from his trap map: the trap's buffNumber
	// gives it when he goes in and takes it away when he comes out (TrapSystem -> startCBuff /
	// endCBuff -> changeAction), exactly like a normal TobiB.
	void onSwitchedChar(const string &name) override
	{
		if (name != HeroEnum::TobiB)
			return;

		unschedule(schedule_selector(CharacterBase::resumeAction));
		setWalkAction(createAnimation(walkArray, 10, true, false));
		if (getOpacity() != 255)
			setOpacity(255);
		setWalkSpeed(224);
		_originSpeed = 224;

		_skillChangeBuffValue = 0;
	}

	void perform() override
	{
		if (isTobiBForm())
		{
			TobiB::perform();
			return;
		}

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

					if (_isCanSkill2 && _mainTarget->getDEF() < 5000 && (_mainTarget->getHpPercent() < 0.5f || !_skillChangeBuffValue))
					{
						changeSide(sp);
						attack(SKILL2);
					}
					else if (_isCanSkill1 && _mainTarget->getDEF() < 5000 && (_mainTarget->getHpPercent() < 0.5f || !_skillChangeBuffValue))
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

	void changeAction() override
	{
		if (isTobiBForm())
		{
			TobiB::changeAction();
			return;
		}

		setWalkAction(createAnimation(skillSPC1Array, 10, true, false));

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

	void resumeAction(float dt) override
	{
		if (isTobiBForm())
		{
			TobiB::resumeAction(dt);
			return;
		}

		setWalkAction(createAnimation(walkArray, 10, true, false));

		if (getOpacity() != 255)
			setOpacity(255);

		setWalkSpeed(224);
		_originSpeed = 224;

		if (_state == State::WALK)
			idle();
		CharacterBase::resumeAction(dt);
	}

	void setActionResume() override
	{
		if (isTobiBForm())
		{
			TobiB::setActionResume();
			return;
		}

		unschedule(schedule_selector(Tobi2::resumeAction));
		setWalkAction(createAnimation(walkArray, 10, true, false));

		if (getOpacity() != 255)
			setOpacity(255);

		setWalkSpeed(224);
		_originSpeed = 224;

		_skillChangeBuffValue = 0;
	}

	/**
	 * Callbacks
	 */

	bool onAcceptAttack(CharacterBase *attacker) override
	{
		if (isTobiBForm())
			return TobiB::onAcceptAttack(attacker);

		if (_skillChangeBuffValue &&
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
};
