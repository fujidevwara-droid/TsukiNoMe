#pragma once
#include "GameOver.h"
#include "GearLayer.h"
#include "PauseLayer.h"
#include "Data/UnitData.h"
#include <memory>

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
#include "glfw3.h"
#include <windows.h>
#define _isPressed(vk_code) (GetAsyncKeyState(vk_code) & 0x8000 ? 1 : 0)
#define isKeyDown(vk_code) (GetAsyncKeyState(vk_code) & 0x8000 ? 1 : 0)
#define getKeyUp(vk_code) (GetAsyncKeyState(vk_code) & 0x8000 ? 0 : 1)
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
#if __has_include("glfw3.h")
#include "glfw3.h"
#elif __has_include(<GLFW/glfw3.h>)
#include <GLFW/glfw3.h>
#elif __has_include("glfw3/include/mac/glfw3.h")
#include "glfw3/include/mac/glfw3.h"
#else
#error "GLFW header not found. Check include paths."
#endif
#define _isPressed(__WINDOW__, __KEY__) glfwGetKey(__WINDOW__, __KEY__)
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "../../../cocos2dx/platform/android/jni/JniHelper.h"
#endif

#include "Systems/CameraZoom.hpp"
#include "Systems/ScreenShake.hpp"
#include "Systems/ScreenVeil.hpp"

class BGLayer;
class CharacterBase;
class Hero;
class Flog;
class Tower;
class GameLayer;
class HudLayer;
class BattleRuntimeSystem;
class SpawnSystem;
class DuelSystem;
struct SessionState;

extern GameLayer *_gLayer;
extern bool _isFullScreen;
#if (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX || CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
static GLFWwindow *_window = nullptr;
#endif

inline GameLayer *getGameLayer()
{
	return _gLayer;
}

class GameLayer : public Layer
{
	using OnHUDInitializedCallback = std::function<void()>;

	friend class LoadLayer;
	friend class BattleRuntimeSystem;
	friend class DuelSystem;

public:
	GameLayer();
	~GameLayer();

	TMXTiledMap *currentMap;
	CharacterBase *currentPlayer;

	uint32_t _second;
	uint32_t _minute;
	int mapId;

	const char *kName;
	const char *aName;
	int kEXPBound;
	int aEXPBound;

	bool _isAttackButtonRelease;
	bool _hasSpawnedGuardian;
	bool _enableGuardian = true;
	// int _guardianNum;
	vector<Flog *> _KonohaFlogArray;
	vector<Flog *> _AkatsukiFlogArray;
	vector<Tower *> _TowerArray;
	vector<Hero *> _CharacterArray;

	bool _isShacking;

	int _playNum;
	void checkBackgroundMusic(float dt);

	PROP(HudLayer *, _hudLayer, HudLayer);
	void onHUDInitialized(const OnHUDInitializedCallback &callback);
	bool isHUDInit();
	void setTowerState(int charId);

	PROP_UInt(totalKills, TotalKills);
	PROP_UInt(totalTime, TotalTime);

	SpriteBatchNode *skillEffectBatch;
	SpriteBatchNode *damageEffectBatch;
	SpriteBatchNode *bulletBatch;
	SpriteBatchNode *shadowBatch;

	bool init();
	void initTileMap();
	void initHeros();
	void initFlogs();
	void initTower();
	void initGard();
	void initEffects();

	void updateViewPoint(float dt);
	void updateGameTime(float dt);

	Hero *addHero(const HeroData &data, int charId);
	Hero *addHero(const string &name, Role role, Group group, Vec2 spawnPoint, int charNo);
	void addFlog(float dt);

	void attackButtonClick(ABType type);
	void gearButtonClick(GearType type);
	void attackButtonRelease();

	void JoyStickRelease();
	void JoyStickUpdate(Vec2 direction);

	PROP(bool, _isSkillFinish, SkillFinish);
	void checkTower();

	void onPause();
	void resumeFromPause();
	void onGear();
	void playGameOpeningAnimation(float dt);
	void onGameStart(float dt);
	void onGameOver(bool isWin);

	// Horizontal walls of the duel arena: nobody may stand left of minX or right
	// of maxX. They are the map edges pulled in by kArenaWallTiles tiles, and
	// additionally never wider than what the (fixed, map-centred) camera shows,
	// so a map wider than the screen cannot let anyone walk out of view. One
	// definition for everything that needs the walls: movement clamps, knockback
	// bounce, AI patrol and retreat.
	static constexpr int kArenaWallTiles = 3;
	static constexpr float kArenaEdgeMargin = 20.0f; // keeps a body, not just its centre, in view
	struct ArenaBounds
	{
		float minX;
		float maxX;
	};
	ArenaBounds arenaBounds() const
	{
		const float tile = currentMap->getTileSize().width;
		const float mapW = currentMap->getMapSize().width * tile;

		ArenaBounds bounds = { kArenaWallTiles * tile, mapW - kArenaWallTiles * tile };

		// The duel camera is centred on the map, so the visible slice of the map
		// is [camLeft, camLeft + screen width]. Same formula as updateViewPoint().
		const float viewW = winSize.width;
		const float camLeft = (mapW - viewW) / 2.0f;
		if (camLeft > 0)
		{
			bounds.minX = (std::max)(bounds.minX, camLeft + kArenaEdgeMargin);
			bounds.maxX = (std::min)(bounds.maxX, camLeft + viewW - kArenaEdgeMargin);
		}
		return bounds;
	}

	// Camera zoom (see Systems/CameraZoom.hpp). Started by the character XML tag
	// <e type="setZoom">factor, seconds</e> through CharacterBase::setZoom().
	CameraZoom _zoom;
	void zoomCamera(float factor, float holdSeconds) { _zoom.trigger(factor, holdSeconds); }
	// Frame-counted version (setZoom2): returns the id release has to be given.
	int zoomCameraHeld(float factor, float failsafeSeconds) { return _zoom.triggerHeld(factor, failsafeSeconds); }
	void releaseZoom(int id) { _zoom.release(id); }

	// Screen shake (see Systems/ScreenShake.hpp): from the XML tag
	// <e type="setShake">strength, seconds</e> through CharacterBase::setShake(), and
	// from the hit shake of the human's ougis. Moves the whole scene, HUD included.
	ScreenShake _shake;
	bool _shakeApplied = false; // the scene is currently offset by the shake
	void shakeScreen(float strength, float seconds) { _shake.trigger(strength, seconds); }
	int shakeScreenHeld(float strength, float failsafeSeconds) { return _shake.triggerHeld(strength, failsafeSeconds); }
	void releaseShake(int id) { _shake.release(id); }
	void applyShake();

	// Screen veil (see Systems/ScreenVeil.hpp): from the XML tags setVeil/setVeil2
	// (a fading colour) and setRoundVeil/setRoundVeil2 (a circle that grows out of
	// the human's hero until it covers the screen) through CharacterBase.
	// Drawn between the game world and the HUD, so the buttons stay clear.
	ScreenVeil _veil;
	LayerColor *_veilLayer = nullptr;
	CCDrawNode *_roundVeilNode = nullptr;
	void showVeil(int r, int g, int b, int a, float seconds, bool round = false) { _veil.trigger(r, g, b, a, seconds, round); }
	int showVeilHeld(int r, int g, int b, int a, float failsafeSeconds, bool round = false) { return _veil.triggerHeld(r, g, b, a, failsafeSeconds, round); }
	void releaseVeil(int id) { _veil.release(id); }
	void applyVeil();

	// Arcade "next stage": the results screen sets these and leaves through
	// onLeft(), which then starts the next match directly instead of handing
	// back to the menu. The team is carried as plain names so nothing depends
	// on the finished match's heroes still existing.
	bool _quickRestartArcade = false;
	string _retainedLeader;
	string _retainedAlly1;
	string _retainedAlly2;

	// Duels / Arcade (see Systems/DuelSystem.hpp)
	DuelSystem &duel() { return *_duel; }
	// Called by the mode handler when a hero dies; resolved next frame.
	void queueDuelDeath(CharacterBase *deadHero);
	void resolveDuelDeaths(float dt);
	static int getDuelMapCount();
	// Polls benched-but-still-finishing heroes (see DuelSystem::bench()).
	void pollDepartingHeroes(float dt);
	// Schedules a selector on this layer without tripping cocos2d's "paused
	// state must match" assert. CCNode::schedule() assumes the layer is paused
	// exactly when it is not running, but an ougi cut-in pauses the scheduler of
	// a layer that is still running; scheduling then (a death, a swap) is fine,
	// the timer simply starts counting when the cut-in ends.
	void scheduleFollowingPause(SEL_SCHEDULE selector, float interval, unsigned int repeat);

	void updateHudSkillButtons();
	// A hero that is not the human's changed form (see Hero::setID). The human's
	// own transforms go through updateHudSkillButtons() instead.
	void onHeroTransformed(CharacterBase *hero);
	void setHPLose(float percent);
	void setEnemyHPLose(float percent);
	void setCKRLose(bool isCRK2);

	void setReport(const string &slayer, const string &dead, uint32_t killNum);
	void clearDoubleClick();
	void resetStatusBar();
	void setCoin(const char *value);
	void removeOugisMark(int type);
	void setOugis(CharacterBase *sender);
	void removeOugis();

	CharacterBase *ougisChar;
	CharacterBase *controlChar;
	Layer *blend;

	void onLeft();

	bool _isSurrender;

	bool _enableGear;
	bool _isOugis2Game;
	bool _isHardCoreGame;
	bool _isRandomChar;

	Group playerGroup;
	bool _isStarted;
	bool _isExiting;

	const char *getGuardianGroup()
	{
		return playerGroup == Group::Konoha ? TowerEnum::AkatsukiCenter : TowerEnum::KonohaCenter;
	}

	bool _isGear;
	bool _isPause;
	GearLayer *_gearLayer;

	void clearAllFlogsMainTarget(CharacterBase *target);
	void clearAllUnitsMainTarget(CharacterBase *target);

	CREATE_FUNC(GameLayer);
	static bool checkHasAnyMovement();
	static int getMapCount();

#if (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX || CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	static void keyEventHandle(GLFWwindow *window, int key, int scancode, int action, int modes);
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	static void keyEventHandle(int key, int keyState);
#endif

private:
	void onEnter();
	void onExit();

	void setKeyEventHandler();
	void removeKeyEventHandler();

#if (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX || CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	int _lastPressedMovementKey;
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	int _lastPressedMovementKey;
#endif
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	static void LPFN_ACCELEROMETER_KEYHOOK(UINT message, WPARAM wParam, LPARAM lParam);
#endif

	void invokeAllCallbacks();

	inline Vec2 getCustomSpawnPoint(HeroData &data);

	bool isHUDInitialized = false;
	bool is4V4Mode = false;
	vector<OnHUDInitializedCallback> callbackssList;

	std::unique_ptr<BattleRuntimeSystem> _battleRuntimeSystem;
	std::unique_ptr<SpawnSystem> _spawnSystem;
	std::unique_ptr<DuelSystem> _duel;
	std::unique_ptr<SessionState> _sessionState;
};

#define BIND(funcName) std::bind(&funcName, this)
