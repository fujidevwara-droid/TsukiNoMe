#pragma once
#include "Core/Utils/Parser.hpp"
#include "Data/UnitData.h"
#include "Effect.h"
#include "GameLayer.h"
#include "HudLayer.h"
#include "MyUtils/KTools.h"

enum class State : uint16_t
{
	DEAD,
	IDLE,
	WALK,
	NATTACK,
	SATTACK,	// Skills
	OATTACK,	// Ougi 1
	O2ATTACK,	// Ougi 2
	HURT,
	KNOCKDOWN,
	FLOAT,
	JUMP,
	AIRHURT
};

class HPBar;
class Hero;

// Ties the two halves of a frame-counted effect (setZoom2 / setShake2) together:
// the step that starts it writes `id` when it runs, the step placed after the
// last frame reads it to end exactly that effect. `failsafe` is how long the
// effect may run if that second step is never reached (an interrupted animation).
struct FrameLink
{
	int id = 0;
	float failsafe = 6.0f;
};

class CharacterBase : public Sprite
{
	friend class CommandSystem;

	using ActionMap = Map<ActionFlag, FiniteTimeAction*>;

public:
	CharacterBase();

	virtual void		setID(const string& name, Role role, Group group);
	virtual void		setHPbar();
	virtual void		changeHPbar();
	virtual void		setShadows();

	PPROP(string, _name, Name);
	PPROP(Role, _role, Role);
	PPROP(Group, _group, Group);
	VPROP(int, _charId, CharId);

	VPROP(State, _state, State);

	uint32_t			_deadNum;
	uint32_t			_flogNum;
	PROP_UInt(_killNum, KillNum);
	PROP_UInt(_coin, Coin);


	CharacterBase* _slayer;
	CharacterBase* _sticker;
	RefPtr<CharacterBase>	_mainTarget;

	bool				isHurtingTower;

	bool				_isFlipped;
	bool				_isHitOne;
	bool				_isCatchOne;
	bool				_isHealing;
	bool				_isVisable;

	// buff
	uint32_t			_healBuffValue;
	uint32_t			_dehealBuffValue;
	uint32_t			_powerUPBuffValue;
	uint32_t			_skillUPBuffValue;
	uint32_t			_skillChangeBuffValue;


	Sprite* _heartEffect;
	Effect* _healBuffEffect;
	Effect* _powerBuffEffect;
	Effect* _skillBuffEffect;
	Sprite* _dehealBuffEffect;
	Effect* _healItemEffect;
	Effect* _speedItemEffect;
	float				_buffStartTime;
	float				_debuffStartTime;
	VPROP(int, _knockLength, KnockLength);
	VPROP(bool, _knockDirection, KnockDirection);

	bool				_isAllAttackLocked;
	bool				_isOnlySkillLocked;
	bool				_isInvincible;
	bool				_isTaunt;
	// Armor : 霸体
	bool				_isArmored;
	bool				_isSuicide;

	bool				_isSticking;
	bool				_isPausing;

	Vec2				_markPoint;
	Vec2				_startPoint; // NOTE: Only for Minano

	bool				_isAI;
	bool				_isControlled;

	PROP_Vector(vector<GearType>, _gearArray, GearArray);

	bool				enableDead = true;
	bool				enableReborn = true;

	// Duels/Arcade bench. A benched hero stays spawned but is hidden, untargetable,
	// AI-less and frozen until it is swapped in. See enterBench()/leaveBench().
	bool				_isBenched = false;

	// Duels / Arcade AI state.
	// Direction the AI patrols in while it has nothing to chase (-1, 0, 1).
	int8_t				_duelWanderDir = 0;
	// True while a human is holding the ramen button to charge the pool. Walking
	// is ignored meanwhile (every other action still works and ends the charge).
	bool				_isHoldingChakraCharge = false;
	// An Ino that is still on the field only so she can be hit while the enemy
	// plays another hero (see DuelSystem). She does not act while it is set.
	bool				_isArenaHusk = false;
	// AI "hold the ramen button" charge, see tryAIChargeChakra().
	bool				_isAIChargingChakra = false;
	float				_aiChakraChargeHeld = 0.0f;
	uint32_t			_nextChakraChargeAttemptSecond = 0;
	// How the arena AI decides to charge chakra (see tryAIChargeChakra()).
	static constexpr uint32_t kAIChargeAttemptSeconds = 10;  // one decision this often
	static constexpr int kAIChargeChancePercent = 50;        // chance a decision starts a charge
	static constexpr uint32_t kAIChargeMaxBars = 2;          // only while the pool holds this many bars or fewer
	static constexpr int kAIChargeSelfInterruptPercent = 10; // chance a charge is abandoned early, on the AI's own
	// The one charge in progress: whether it will be abandoned early, and when.
	bool				_aiChargeSelfInterrupts = false;
	// A roll that succeeded while the hero was mid-action: the charge starts as
	// soon as it is free to (see tryAIChargeChakra), but not later than this.
	bool				_aiChargeDue = false;
	uint32_t			_aiChargeDueUntil = 0;
	static constexpr uint32_t kAIChargeWaitSeconds = 6;
	float				_aiChargeInterruptAt = 0.0f;
	int					changeCharId = -1; // NOTE: Support for random deathmatch mode

	int					rebornLabelTime;
	int					battleCondiction;
	// True is only has one tower, false otherwise
	bool				isBaseDanger;


	HPBar* _hpBar;
	Sprite* _shadow;
	float				_originY;
	float				_backY;
	float				_diretionY;

	int					lbAttackerId = -1;
	uint32_t			lostBloodValue;

	// ---- Poison ----------------------------------------------------------
	// XML: <e type="setPoison">30, 3</e> = 30 damage per tick for 3 seconds.
	// setPoison() only ARMS the attacker; every victim hit while the arm is
	// live gets poisoned (so area attacks poison all targets), and the arm
	// expires by itself so a missed attack cannot poison a later, unrelated hit.
	static constexpr float kPoisonTickSeconds = 0.5f;	// DOT tick interval
	static constexpr float kPoisonArmSeconds = 0.6f;	// how long an armed poison stays ready

	// On the attacker:
	uint32_t			_pendingPoisonValue = 0;		// damage per tick (0 = not armed)
	float				_pendingPoisonSeconds = 3.0f;	// how long the DOT lasts

	// On the victim:
	int					poisonAttackerId = -1;			// who gets the credit
	uint32_t			poisonTickValue = 0;
	int					poisonTicksLeft = 0;
	bool				_inPoisonTick = false;			// DOT damage must not re-apply poison
	int					gearCKRValue;
	uint32_t			gearRecoverValue;

	// 忍び：Kakuzu ability
	uint32_t			hearts;
	// Gear02 ability
	bool				isAttackGainCKR;
	// 口寄せ：Bikyu ability
	// 忍び：Guardian & Naruto-Rikutou & Naruto-Sage ability
	bool				hasArmorBroken;
	// Gear ability
	bool				hasArmor;
	bool				isGearCD;

	bool				_isCanGear00;
	bool				_isCanGear03;
	bool				_isCanGear06;

	VPROP(int, _walkSpeed, WalkSpeed);
	int					_originSpeed;

	PROP_UInt(_hp, HP);
	PROP_UInt(_maxHP, MaxHP);
	PROP_UInt(_ckr, CKR);
	PROP_UInt(_ckr2, CKR2);
	VPROP(int, _defense, DEF);
	VPROP(float, _exp, EXP);
	VPROP(uint32_t, _level, LV);
	VPROP(int, _height, Height);
	VPROP(int, _rebornTime, RebornTime);
	VPROP(Vec2, _spawnPoint, SpawnPoint);


	VPROP(CharacterBase*, _master, Master);
	VPROP(CharacterBase*, _controller, Controller);
	VPROP(CharacterBase*, _secmaster, SecMaster);

	PROP_REF(string, _attackType, AttackType);
	uint32_t		_attackValue;
	int				_attackRangeX;
	int				_attackRangeY;

	// int totalCombatPoint;
	int				enemyCombatPoint;
	int				friendCombatPoint;

	PROP_UInt(_nAttackValue, NAttackValue);
	PROP_REF(string, _nAttackType, NAttackType);
	int				_nAttackRangeX;
	int				_nAttackRangeY;

	PROP_UInt(_sAttackValue1, SAttackValue1);
	PROP_REF(string, _sAttackType1, SAttackType1);
	int				_sAttackRangeX1;
	int				_sAttackRangeY1;
	uint32_t		_sAttackCD1;
	bool			_sAttack1isDouble;
	int				_sAttackCombatPoint1;

	PROP_UInt(_sAttackValue2, SAttackValue2);
	PROP_REF(string, _sAttackType2, SAttackType2);
	int				_sAttackRangeX2;
	int				_sAttackRangeY2;
	uint32_t		_sAttackCD2;
	bool			_sAttack2isDouble;
	int				_sAttackCombatPoint2;

	PROP_UInt(_sAttackValue3, SAttackValue3);
	PROP_REF(string, _sAttackType3, SAttackType3);
	int				_sAttackRangeX3;
	int				_sAttackRangeY3;
	uint32_t		_sAttackCD3;
	bool			_sAttack3isDouble;
	int				_sAttackCombatPoint3;

	PROP_UInt(_sAttackValue4, SAttackValue4);
	PROP_REF(string, _sAttackType4, SAttackType4);
	int				_sAttackRangeX4;
	int				_sAttackRangeY4;
	uint32_t		_sAttackCD4;
	bool			_sAttack4isDouble;
	int				_sAttackCombatPoint4;

	PROP_UInt(_sAttackValue5, SAttackValue5);
	PROP_REF(string, _sAttackType5, SAttackType5);
	int				_sAttackRangeX5;
	int				_sAttackRangeY5;
	uint32_t		_sAttackCD5;
	int				_sAttackCombatPoint5;

	PROP_UInt(_spcAttackValue1, SpcAttackValue1);
	PROP_REF(string, _spcAttackType1, SpcAttack1Type);
	int				_spcAttackRangeX1;
	int				_spcAttackRangeY1;
	uint32_t		_spcAttackCD1;

	PROP_UInt(_spcAttackValue2, SpcAttackValue2);
	PROP_REF(string, _spcAttackType2, SpcAttack2Type);
	int				_spcAttackRangeX2;
	int				_spcAttackRangeY2;
	uint32_t		_spcAttackCD2;

	PROP_UInt(_spcAttackValue3, SpcAttackValue3);
	PROP_REF(string, _spcAttackType3, SpcAttack3Type);
	int				_spcAttackRangeX3;
	int				_spcAttackRangeY3;
	uint32_t		_spcAttackCD3;

	// Extended skill slots (skillSPC6-15): data only, no HUD button or click
	// dispatch, same as skillSPC1-5. They carry the combat stats (type, value,
	// range, cooldown) for character-specific mechanics that need more than the
	// original five special slots. Hero.hpp fills them from the character's XML.
	PROP_UInt(_spcAttackValue6, SpcAttackValue6);
	PROP_REF(string, _spcAttackType6, SpcAttack6Type);
	int				_spcAttackRangeX6 = 0;
	int				_spcAttackRangeY6 = 0;
	uint32_t		_spcAttackCD6 = 0;

	PROP_UInt(_spcAttackValue7, SpcAttackValue7);
	PROP_REF(string, _spcAttackType7, SpcAttack7Type);
	int				_spcAttackRangeX7 = 0;
	int				_spcAttackRangeY7 = 0;
	uint32_t		_spcAttackCD7 = 0;

	PROP_UInt(_spcAttackValue8, SpcAttackValue8);
	PROP_REF(string, _spcAttackType8, SpcAttack8Type);
	int				_spcAttackRangeX8 = 0;
	int				_spcAttackRangeY8 = 0;
	uint32_t		_spcAttackCD8 = 0;

	PROP_UInt(_spcAttackValue9, SpcAttackValue9);
	PROP_REF(string, _spcAttackType9, SpcAttack9Type);
	int				_spcAttackRangeX9 = 0;
	int				_spcAttackRangeY9 = 0;
	uint32_t		_spcAttackCD9 = 0;

	PROP_UInt(_spcAttackValue10, SpcAttackValue10);
	PROP_REF(string, _spcAttackType10, SpcAttack10Type);
	int				_spcAttackRangeX10 = 0;
	int				_spcAttackRangeY10 = 0;
	uint32_t		_spcAttackCD10 = 0;

	PROP_UInt(_spcAttackValue11, SpcAttackValue11);
	PROP_REF(string, _spcAttackType11, SpcAttack11Type);
	int				_spcAttackRangeX11 = 0;
	int				_spcAttackRangeY11 = 0;
	uint32_t		_spcAttackCD11 = 0;

	PROP_UInt(_spcAttackValue12, SpcAttackValue12);
	PROP_REF(string, _spcAttackType12, SpcAttack12Type);
	int				_spcAttackRangeX12 = 0;
	int				_spcAttackRangeY12 = 0;
	uint32_t		_spcAttackCD12 = 0;

	PROP_UInt(_spcAttackValue13, SpcAttackValue13);
	PROP_REF(string, _spcAttackType13, SpcAttack13Type);
	int				_spcAttackRangeX13 = 0;
	int				_spcAttackRangeY13 = 0;
	uint32_t		_spcAttackCD13 = 0;

	PROP_UInt(_spcAttackValue14, SpcAttackValue14);
	PROP_REF(string, _spcAttackType14, SpcAttack14Type);
	int				_spcAttackRangeX14 = 0;
	int				_spcAttackRangeY14 = 0;
	uint32_t		_spcAttackCD14 = 0;

	PROP_UInt(_spcAttackValue15, SpcAttackValue15);
	PROP_REF(string, _spcAttackType15, SpcAttack15Type);
	int				_spcAttackRangeX15 = 0;
	int				_spcAttackRangeY15 = 0;
	uint32_t		_spcAttackCD15 = 0;

	PROP_UInt(_tempAttackValue1, TempAttackValue1);


	PROP_REF(string, _effectType, EffectType);
	bool			_hurtFromLeft;
	bool			_hurtFromRight;

	int						_damageDisplayCount = 0;
	uint32_t				damageEffectCount;
	PROP_Vector(vector<CharacterBase*>, _monsterArray, MonsterArray);

	// movement
	PROP(Vec2, _velocity, Velocity);
	PROP(Vec2, _desiredPosition, DesiredPosition);

	// actions
	PPROP_PTR(FiniteTimeAction, _idleAction, IdleAction);
	PPROP_PTR(FiniteTimeAction, _nAttackAction, NAttackAction);
	PPROP_PTR(FiniteTimeAction, _walkAction, WalkAction);
	PPROP_PTR(FiniteTimeAction, _hurtAction, HurtAction);
	PPROP_PTR(FiniteTimeAction, _knockDownAction, KnockDownAction);
	PPROP_PTR(FiniteTimeAction, _airHurtAction, AirHurtAction);
	PPROP_PTR(FiniteTimeAction, _floatAction, FloatAction);
	PPROP_PTR(FiniteTimeAction, _deadAction, DeadAction);

	PPROP_PTR(FiniteTimeAction, _skill1Action, Skill1Action);
	PPROP_PTR(FiniteTimeAction, _skill2Action, Skill2Action);
	PPROP_PTR(FiniteTimeAction, _skill3Action, Skill3Action);
	PPROP_PTR(FiniteTimeAction, _skill4Action, Skill4Action);
	PPROP_PTR(FiniteTimeAction, _skill5Action, Skill5Action);

	PPROP_PTR(FiniteTimeAction, _moveAction, MoveAction);
	PPROP_PTR(FiniteTimeAction, _floatUPAction, FloatUPAction);
	PPROP_PTR(FiniteTimeAction, _jumpUPAction, JumpUPAction);


	void				update(float dt);
	void				updateHpBarPosition(float dt);
	void				acceptAttack(Ref* object);

	float				getHpPercent();

	bool				checkHasMovement();

	void				idle();
	void				walk(Vec2 direction);
	void				attack(ABType type);
	void				sAttack(ABType type);
	void				nAttack();
	void				oAttack(ABType type);
	bool				hurt();
	bool				hardHurt(int delayTime, bool isHurtAction, bool isCatch, bool isStick, bool isStun);
	// bool				hardHurt(int delayTime, bool isHurtAction, HardHurtState state);
	void				absorb(Vec2 position, bool isImmediate);
	void				jump() {}; // No reference
	void				knockDown();
	virtual void		dead();
	void				floatUP(float floatHeight, bool isCancelSkill);
	void				airHurt();

	virtual void		doAI();


	virtual void		changeAction();
	void				changeAction2();
	void				changeGroup();
private:
	template <typename T>
	typename std::enable_if<std::is_base_of<CharacterBase, T>::value, void>::type
		changeGroupBy(const vector<T*>& list);
public:
	virtual void		resumeAction(float dt);
	virtual void		setActionResume();
	void				setActionResume2();
	void				reCatched(float dt);

	void				setShadow(SpriteFrame* frame);
	void				enableShadow(Sprite* charN);
	void				disableShadow(Sprite* charN);

	void				setItem(ABType type);
	void				disableEffect();
	void				disableHpBar(float dt);

	void				disableShack();
	void				disableDebuff(float dt);

	void				enableSkill1(float dt);
	void				enableSkill2(float dt);
	void				enableSkill3(float dt);
	void				enableItem1(float dt);

	void				enableGear00(float dt);
	void				enableGear03(float dt);
	void				enableGear06(float dt);

	void				disableGear1(float dt);
	void				disableGear2(float dt);
	void				disableGear3(float dt);

	bool				setGear(GearType type);
	void				useGear(GearType type);

	void				setCoinDisplay(int num);
	void				removeCoinDisplay(Sprite* coinDisplay);
	void				addCoin(uint32_t num);
	void				minusCoin(uint32_t num);

	bool				_isCanSkill1;
	bool				_isCanSkill2;
	bool				_isCanSkill3;
	bool				_isCanItem1;
	bool				_isCanOugis1;
	bool				_isCanOugis2;

	CCArray* idleArray;
	CCArray* walkArray;
	CCArray* hurtArray;
	CCArray* airHurtArray;
	CCArray* knockDownArray;
	CCArray* floatArray;
	CCArray* deadArray;
	CCArray* nattackArray;
	CCArray* skill1Array;
	CCArray* skill2Array;
	CCArray* skill3Array;
	CCArray* skill4Array;
	CCArray* skill5Array;

	CCArray* skillSPC1Array;
	CCArray* skillSPC2Array;
	CCArray* skillSPC3Array;
	CCArray* skillSPC4Array;
	CCArray* skillSPC5Array;
	CCArray* skillSPC6Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC7Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC8Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC9Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC10Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC11Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC12Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC13Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC14Array = nullptr; // null until the XML has that slot
	CCArray* skillSPC15Array = nullptr; // null until the XML has that slot


	virtual void		dealloc();

	// Put the hero on / take it off the Duels bench. Neither one despawns the
	// hero or anything it summoned; only a dead hero is ever removed.
	void				enterBench();

	// Start of a bench swap for a hero that is in the middle of an action: it is
	// taken out of play at once (no AI, no control, cannot be hit) but stays
	// visible, so the action can play out. enterBench() finishes the job.
	void				beginDeparture();

	// Kills the units that need this hero to control them (Akamaru, Parents,
	// Karasu). Their own death logic then ends the owner's linked action. Every
	// other summon, puppet, Mon and projectile is deliberately left alone: they
	// keep running while their owner is benched.
	void				killOwnedSummons();

	// For Ino: tells the arena that a possession is over, so what she held goes
	// back to its owner's bench. Does nothing outside the duel modes.
	void				releaseArenaCaptive();

	// Colours this hero's floating HP bar by which side it is on: blue for the
	// human's team, red for the other. changeGroup() decides by comparing with
	// currentPlayer, which is wrong whenever the hero being compared against is the
	// possessed body itself (the human is steering it), so the arena sets the colour
	// from the human's own team instead.
	void				refreshTeamBarColor();

	// Puts the hero into a real standing pose right now. idle() alone is not
	// enough while movement keys are held: it skips the idle animation (expecting
	// walk() to restart at once), which leaves a frozen walk frame when walking
	// has been switched off. Also clears any leftover walking velocity.
	void				forceIdleStance();

	// The AI's stand-in for holding the ramen button: now and then, while idle
	// and low on chakra, it charges the shared pool the same way a player does.
	void				tryAIChargeChakra();
	void				updateAIChargeChakra(float dt);
	void				stopAIChargeChakra();
	void				leaveBench(const Vec2 &pos, bool flipped);
	virtual void		reborn(float dt);


	void				resumePauseStuff(float dt);

	void				lostBlood(float dt);
	void				removeLostBlood(float dt);

	// Poison DOT (see fields above).
	void				setPoison(const string& value);
	void				expireArmedPoison(float dt);
	void				applyPoisonFrom(CharacterBase* attacker, CharacterBase* credit);
	void				poisonTick(float dt);
	void				removePoison(float dt);

	virtual void		setAI(float dt);
	virtual void		setRestore2(float dt);


	void				readData(CCArray* tmpData, string& attackType, uint32_t& attackValue, int& attackRangeX, int& attackRangeY, uint32_t& cooldown, int& combatPoint);
	FiniteTimeAction* createAnimation(CCArray* ationArray, uint8_t fps, bool isLoop, bool isReturnToIdle);
	FiniteTimeAction* createAnimation(CCArray* arr, const ActionConstant::AnimationInfo& info) { return createAnimation(arr, info.fps, info.isLoop, info.isReturnToIdle); }
	FiniteTimeAction* createAnimIdle(CCArray* arr) { return createAnimation(arr, ActionConstant::Idle); }
	FiniteTimeAction* createAnimKnockdown(CCArray* arr) { return createAnimation(arr, ActionConstant::Knockdown); }
	FiniteTimeAction* createAnimRegular(CCArray* arr) { return createAnimation(arr, ActionConstant::Regular); }
	FiniteTimeAction* createAnimSkill(CCArray* arr) { return createAnimation(arr, ActionConstant::Skill); }

	void				setSound(const string& file);
	void				setDSound(const string& file);
	void				setMove(int moveLength);
	void				setCharge(int moveLength);
	void				setChargeB(int moveLength);
	void				getCollider();
	void				getSticker(float dt);
	void				stopMove(float dt);
	void				stopJump(int stopTime);
	void				setCharFlip();
	void				setAttackBox(const string& effectType);
	inline void			setDamage(CharacterBase* attacker);
	void				setDamage(CharacterBase* attacker, const string& effectType, int attackValue, bool isFlipped);
	void				setDamgeDisplay(int value, const char* font);

	void				setSkillEffect(const string& type);

	// XML <e type="setZoom">factor, seconds</e>: zooms the camera in on the hero the
	// human is playing, holds it, and eases back after `seconds`. Ignored for every
	// other hero, so an enemy's skill never moves your camera.
	void				setZoom(const string& args);

	// XML <e type="setShake">strength, seconds</e>: shakes the screen. Like setZoom,
	// only the hero the human is playing does it.
	void				setShake(const string& args);

	// XML <e type="setZoom2">factor, frames</e> and <e type="setShake2">strength, frames</e>:
	// the same effects, but they last for a number of the animation's frames (the
	// <f> entries after the tag) instead of for seconds. With no number they last
	// until the animation has finished. Resolved while the animation is built, see
	// createAnimation().
	void				setZoomUntil(float factor, std::shared_ptr<FrameLink> link);
	void				releaseZoomLink(std::shared_ptr<FrameLink> link);
	void				setShakeUntil(float strength, std::shared_ptr<FrameLink> link);
	void				releaseShakeLink(std::shared_ptr<FrameLink> link);
	// void				setItemEffect(const string &type);
	void				setDamgeEffect(const string& type);
	void				setMonAttack(int skillNum);
	void				setTransform();
	void				setOugis();

	virtual Hero* createClone(int cloneTime);
	void				setClone(int cloneTime);
	void				setMon(const string& monName);
	void				setMonPer(float dt);
	void				setBullet(const string& bulletName);
	void				setBulletGroup(float dt);
	void				setTrap(const string& trapName);

	void				removeAllClones();

	void				setBuff(int buffValue);
	void				setCommand(const string& cmd);
	void				setBuffEffect(const string& type);
	void				removeBuffEffect(const string& type);

	void				disableBuff(float dt);
	void				healBuff(float dt);
	void				dehealBuff(float dt);

protected:
	void				removeClone(float dt);
	void				removeSelf(float dt);
	void				setJump(bool jumpDirection);

	CCRect				setHalfBox();
	CCRect				setHitBox();

	void				checkActionFinish(float dt);

	void				setRestore(float dt);


	bool				findEnemy(Role role, int searchRange, bool masterRange = false);
	bool				findEnemy2(Role role);
	bool				findTargetEnemy(Role role, bool isTowerDected);
	template <typename T>
	typename std::enable_if<std::is_base_of<CharacterBase, T>::value, bool>::type
		findEnemyBy(const vector<T*>& list, int searchRange, bool masterRange = false);
	template <typename T>
	typename std::enable_if<std::is_base_of<CharacterBase, T>::value, bool>::type
		findEnemy2By(const vector<T*>& list);
	template <typename T>
	typename std::enable_if<std::is_base_of<CharacterBase, T>::value, bool>::type
		findTargetEnemyBy(const vector<T*>& list, bool isTowerDected);
	bool				checkBase();

	bool				stepBack();
	bool				stepBack2();
	bool				checkRetri();
	void				stepOn();
	void				changeSide(Vec2 sp);

	inline void			autoFlip(CharacterBase* attacker);

	void				removeDamageDisplay(CCNode* damageDisplay);

public:
	bool hasTempAttackValue1() { return getTempAttackValue1() > 0; }
	// UI
	void updateHpBar();
	void updateDataByLVOnly();
	void lockSkill4Button() {
		if (isPlayer())
			getGameLayer()->getHudLayer()->skill4Button->setLock();
	}
	void unlockSkill4Button() {
		if (isPlayer())
			getGameLayer()->getHudLayer()->skill4Button->unLock();
	}
	void lockSkill5Button() {
		if (isPlayer())
			getGameLayer()->getHudLayer()->skill5Button->setLock();
	}
	void unlockSkill5Button() {
		if (isPlayer())
			getGameLayer()->getHudLayer()->skill5Button->unLock();
	}
	void lockOugisButtons() {
		if (isPlayer()) {
			getGameLayer()->getHudLayer()->skill4Button->setLock();
			getGameLayer()->getHudLayer()->skill5Button->setLock();
		}
	}
	// The skill 4/5 buttons on the HUD are one shared pair, not one pair per hero.
	// A hero that keeps them locked for its own reasons (a puppet that is still
	// out, say) overrides this to lock them again from its own state whenever it
	// takes over the HUD, e.g. when it is swapped in. Nothing for most heroes.
	virtual void reapplyOugisLocks() {}
	void unlockOugisButtons() {
		if (isPlayer()) {
			getGameLayer()->getHudLayer()->skill4Button->unLock();
			getGameLayer()->getHudLayer()->skill5Button->unLock();
		}
	}
	// character extensions
	void setHPValue(uint32_t var, bool isUpdateHpBar = true) {
		setHP(var);
		if (isUpdateHpBar)
			updateHpBar();
	}
	void setMaxHPValue(uint32_t var, bool isUpdateHpBar = true) {
		setMaxHP(var);
		if (isUpdateHpBar)
			updateHpBar();
	}
	void resetDefenseValue(int def) {
		if (_defense > 0 && _defense > def)
			_defense -= def;
		else
			_defense = 0;
	}
	bool isGuardian() { return _name == GuardianEnum::Han || _name == GuardianEnum::Roshi; }
	bool isNotGuardian() { return !isGuardian(); }
	// role extensions
	bool isCom() { return _role == Role::Com; }
	bool isPlayer() { return _role == Role::Player; }
	bool isPlayerOrCom() { return isPlayer() || isCom(); }
	// True while this specific body is the one the human is actively
	// piloting. Differs from isPlayer() (which checks the fixed spawn
	// _role) whenever a possession (e.g. Ino's Shintenshin) has put the
	// human in control of a body whose _role is not Role::Player - such as
	// a possessed enemy. Action-readiness gates (skill-finish flag, forced
	// idle state, attack unlocking) should key off this instead of
	// isPlayer(), or a possessed non-Player body's skills silently refuse
	// to fire even though its buttons are clickable.
	bool isActivePlayerBody() { return getGameLayer()->currentPlayer == this; }
	bool isFlog() { return _role == Role::Flog; }
	bool isTower() { return _role == Role::Tower; }
	bool isBullet() { return _role == Role::Bullet; }
	bool isClone() { return _role == Role::Clone; }
	bool isKugutsu() { return _role == Role::Kugutsu; }
	bool isMon() { return _role == Role::Mon; }
	bool isSummon() { return _role == Role::Summon; }
	bool isNotCom() { return _role != Role::Com; }
	bool isNotPlayer() { return _role != Role::Player; }
	bool isNotFlog() { return _role != Role::Flog; }
	bool isNotTower() { return _role != Role::Tower; }
	bool isNotBullet() { return _role != Role::Bullet; }
	bool isNotClone() { return _role != Role::Clone; }
	bool isNotKugutsu() { return _role != Role::Kugutsu; }
	bool isNotMon() { return _role != Role::Mon; }
	bool isNotSummon() { return _role != Role::Summon; }
	// group extensions
	bool isKonohaGroup() { return _group == Group::Konoha; }
	bool isAkatsukiGroup() { return _group == Group::Akatsuki; }
	// monster extensions
	bool hasMonsterArrayAny() { return !_monsterArray.empty(); }
	void removeMon(CharacterBase* mo) {
		if (hasMonsterArrayAny())
			std::erase(_monsterArray, mo);
	}
	void removeAllMonAndCleanup(const string& name) {
		if (hasMonsterArrayAny()) {
			for (auto mo : _monsterArray)
			{
				if (mo->getName() == name)
				{
					std::erase(_monsterArray, mo);
					mo->removeFromParent();
				}
			}
		}
	}
	// State extensions

	// State is IDLE, WALK or NATTACK
	bool isFreeState() {
		return _state == State::IDLE || _state == State::WALK || _state == State::NATTACK;
	}
	// Action extensions
	bool hasAction(ActionFlag action) {
		return (_actionFlag & action) != ActionFlag::None;
	}
	// Utilities
	void increaseAllCkrs(uint32_t value, bool enableLv2 = true, bool enableLv4 = true);
	void increaseHpAndUpdateUI(uint32_t value);
protected:
	// find enemy extensions
	bool findHero(int searchRange, bool masterRange = false) { return findEnemy(Role::Hero, searchRange, masterRange); }
	bool findFlog(int searchRange, bool masterRange = false) { return findEnemy(Role::Flog, searchRange, masterRange); }
	bool findTower(int searchRange, bool masterRange = false) { return findEnemy(Role::Tower, searchRange, masterRange); }
	bool notFindHero(int searchRange, bool masterRange = false) { return !findEnemy(Role::Hero, searchRange, masterRange); }
	bool notFindFlog(int searchRange, bool masterRange = false) { return !findEnemy(Role::Flog, searchRange, masterRange); }
	bool notFindTower(int searchRange, bool masterRange = false) { return !findEnemy(Role::Tower, searchRange, masterRange); }
	bool findHeroHalf() { return findEnemy2(Role::Hero); }
	bool findFlogHalf() { return findEnemy2(Role::Flog); }
	bool findTowerHalf() { return findEnemy2(Role::Tower); }
	bool notFindHeroHalf() { return !findEnemy2(Role::Hero); }
	bool notFindFlogHalf() { return !findEnemy2(Role::Flog); }
	bool notFindTowerHalf() { return !findEnemy2(Role::Tower); }
	// AI extensions
	void tryBuyGear(GearType gear1, GearType gear2, GearType gear3) {
		if (getCoin() >= 500 && getGearArray().size() < 3 && !_isControlled && getGameLayer()->_enableGear)
		{
			if (getGearArray().size() == 0)
				setGear(gear1);
			else if (getGearArray().size() == 1)
				setGear(gear2);
			else if (getGearArray().size() == 2)
				setGear(gear3);
		}
	}
	void tryUseGear6() {
		if (_isCanGear06)
		{
			if ((_state == State::FLOAT ||
				_state == State::AIRHURT ||
				_state == State::HURT ||
				_state == State::KNOCKDOWN) &&
				getHpPercent() < 0.5 && !_isArmored && !_isInvincible)
			{
				useGear(GearType::Gear06);
			}
		}
	}
	bool needBackToTowerToRestoreHP(bool isNeedBack = true) {
		if (checkRetri() && isNeedBack)
		{
			if (_mainTarget)
			{
				if (stepBack2())
				{
					if (_isCanGear00 && !_isArmored)
						useGear(GearType::Gear00);
					return true;
				}
			}
			else
			{
				if (_isCanGear00)
					useGear(GearType::Gear00);

				if (stepBack())
					return true;
			}
		}
		return false;
	}
	bool needBackToDefendTower(bool isNeedBack = true) {
		if (isBaseDanger && checkBase() && !_isControlled && isNeedBack)
		{
			bool needBack = false;
			if (isAkatsukiGroup())
			{
				if (getPositionX() < 85 * 32)
					needBack = true;
			}
			else
			{
				if (getPositionX() > 11 * 32)
					needBack = true;
			}

			if (needBack)
			{
				if (_isCanGear00)
					useGear(GearType::Gear00);

				if (stepBack2())
					return true;
			}
		}
		return false;
	}
	bool checkMove(float x = 32, float y = 32) {
		auto sp = getDistanceToTarget();
		if (abs(sp.x) > x || abs(sp.y) > y)
		{
			auto moveDirection = sp.getNormalized();
			walk(moveDirection);
			return true;
		}
		return false;
	}
	void checkHealingState() {
		if (_isHealing && getHpPercent() < 1)
		{
			if (isFreeState())
				idle();
		}
		else
		{
			if (_isCanGear00 && !_isArmored)
				useGear(GearType::Gear00);

			stepOn();
		}
	}
	Vec2 getDirByMoveTo(CharacterBase* target) {
		return (target->getPosition() - getPosition()).getNormalized();
	}
	Vec2 getDistanceToTarget() {
		return _mainTarget->_originY
			? Vec2(_mainTarget->getPositionX(), _mainTarget->_originY) - getPosition()
			: _mainTarget->getPosition() - getPosition();
	}
	Vec2 getDistanceToTargetAndIgnoreOriginY() {
		return _mainTarget->getPosition() - getPosition();
	}

	/**
	 * Callbacks
	 */
protected:
	virtual bool onAcceptAttack(CharacterBase* attacker) { return true; }
	// Returns true will call CharacterBase::setDamage(attacker), false otherwise.
	virtual bool onHit(CharacterBase* attacker) { return true; }
	// Returns true will call CharacterBase::setDamage(attacker), false otherwise.
	virtual bool onBulletHit(CharacterBase* attacker) { return true; }

	virtual void onSetTrap(const string& trapType) {}

	void clearActionData() {
		_actionFlag = ActionFlag::None;
		_actionMap.clear();
	}

private:
	ActionFlag	_actionFlag;
	ActionMap	_actionMap;

	bool _affectedByTower;
};