#pragma once
#include "Defines.h"
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "../../../cocos2dx/platform/android/jni/JniHelper.h"
#endif

class GameOver : public Layer
{
public:
	GameOver();
	~GameOver();

	bool init(RenderTexture *snapshoot);

	PROP(bool, _isWin, Win);

	Layer *exitLayer = nullptr;
	Layer *cheatLayer = nullptr;
	Layer *unlockedLayer = nullptr; // "<hero> has been unlocked!" popup
	Sprite *result_bg = nullptr;

	Sprite *refreshBtn = nullptr;
	MenuItem *upload_btn = nullptr;
	MenuItem *start_btn = nullptr; // Arcade: next stage / new run

	string detailRecord;
	float finnalScore;

	static GameOver *create(RenderTexture *snapshoot);

private:
	// An owned string: it used to point into a hero's own name buffer, which
	// is gone (or changed by a transformation) by the time it is read again.
	string resultChar;

	// Heroes unlocked by this match that still need their popup. Shown one at
	// a time so several unlocks at once do not stack on top of each other.
	std::vector<string> _unlockPopupQueue;

	void onBackToMenu(Ref *sender);
	void listResult();
	void onCancel(Ref *sender);
	void onLeft(Ref *sender);
	void onUPloadBtn(Ref *sender);
	void onStartNextStage(Ref *sender);

	void showNextUnlockedPopup();
	void onUnlockedPopupOK(Ref *sender);
};
