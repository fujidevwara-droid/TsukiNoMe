#include "StartMenu.h"
#include "UI/GameModeLayer.h"
#include "UI/ModeMenuButton.hpp"
#include "Constants/UiFlowKeys.hpp"
#include "MyUtils/KTools.h"

extern const GameData kDefaultGameData;

// const GameModeData &GameModeData::from(const char *path)
// {
//     GameModeData data = {}
//     return data;
// }

bool GameModeLayer::init()
{
	auto bgSprite = Sprite::create("red_bg.png");
	FULL_SCREEN_SPRITE(bgSprite);
	bgSprite->setAnchorPoint(Vec2(0, 0));
	bgSprite->setPosition(Vec2(0, 0));
	addChild(bgSprite, -100);

	// menu bars
	auto menu_bar_b = Sprite::create("menu_bar2.png");
	menu_bar_b->setAnchorPoint(Vec2(0, 0));
	FULL_SCREEN_SPRITE(menu_bar_b);
	addChild(menu_bar_b, 2);

	auto menu_bar_t = Sprite::create("menu_bar3.png");
	menu_bar_t->setAnchorPoint(Vec2(0, 0));
	menu_bar_t->setPosition(Vec2(0, winSize.height - menu_bar_t->getContentSize().height));
	FULL_SCREEN_SPRITE(menu_bar_t);
	addChild(menu_bar_t, 2);

	auto modemenu_title = Sprite::createWithSpriteFrameName("startmenu_title.png");
	modemenu_title->setAnchorPoint(Vec2(0, 0));
	modemenu_title->setPosition(Vec2(2, winSize.height - modemenu_title->getContentSize().height - 2));
	addChild(modemenu_title, 3);

	initModeData();

	// init menus
	// const int padding = -10;
	// const int width = 100;
	const int offset = (winSize.width - 460) / 2 + 100 / 2;
	const float posY = (winSize.height / 2) + 30;
	// The menu has eight fixed slots, each with its own artwork
	// (GameMode/<art + 1>.png). Duels and Arcade are real modes of their own but
	// temporarily take the slots that Boss and Deathmatch used to occupy.
	struct MenuSlot
	{
		GameMode mode;
		int art;
	};
	static const MenuSlot kSlots[] = {
		{GameMode::OneVsOne, 0},
		{GameMode::Classic, 1},
		{GameMode::FourVsFour, 2},
		{GameMode::HardCore_4Vs4, 3},
		{GameMode::Duels, 4},
		{GameMode::Clone, 5},
		{GameMode::Arcade, 6},
		{GameMode::RandomDeathmatch, 7},
	};

	for (const auto &slot : kSlots)
	{
		const int art = slot.art;
		auto mode_btn = ModeMenuButton::create(format("GameMode/{}.png", art + 1));
		mode_btn->mode = slot.mode;
		mode_btn->setDelegate(this);

		if (art < 3) // left column
		{
			mode_btn->setPositionX(offset);
			mode_btn->setPositionY((posY + 55 + 7.5f) - art * (55 + 7.5f));
		}
		else if (art < 6) // middle row
		{
			mode_btn->setPositionX(offset + 10 + (art - 2) * (80 + 5));
			mode_btn->setPositionY(posY);
		}
		else // right column
		{
			mode_btn->setPositionX(offset + 20 + (80 + 5) * 4);
			mode_btn->setPositionY((posY + 47) - (art - 6) * (86 + 8.0f));
		}

		menuButtons[slot.mode] = mode_btn;
		addChild(mode_btn);
	}

	for (size_t i = 0; i < menuButtons.size(); i++)
	{
		// Boss and Deathmatch have no button while Duels/Arcade stand in for them.
		if (menuButtons[i] && modes[i].isLocked)
		{
			menuButtons.at(i)->useMask2 = modes.at(i).useMask2;
			menuButtons.at(i)->lock();
		}
	}

	menuLabel = CCLabelTTF::create();
	menuLabel->setAnchorPoint(Vec2(0, 0));
	menuLabel->setPosition(Vec2(10, 2));
	addChild(menuLabel, 5);

	// init return button
	auto return_img = MenuItemSprite::create(Sprite::create("UI/return_btn.png"), nullptr, nullptr, this, menu_selector(GameModeLayer::backToMenu));
	Menu *return_btn = Menu::create(return_img, nullptr);
	return_btn->setAnchorPoint(Vec2(1, 0.5f));
	return_btn->setPosition(winSize.width - 38, 65);
	addChild(return_btn, 5);

	return Layer::init();
}

void GameModeLayer::backToMenu(Ref *sender)
{
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/cancel.ogg");

	auto menuScene = Scene::create();
	auto menuLayer = StartMenu::create();
	menuScene->addChild(menuLayer);
	Director::sharedDirector()->replaceScene(TransitionFade::create(1.0f, menuScene));
}

void GameModeLayer::initModeData()
{
	// init mode text data
	auto lang = Application::sharedApplication()->getCurrentLanguage();
	if (lang == LanguageType::kLanguageChinese)
	{
		modes[GameMode::OneVsOne] = {"1 VS 1", ""};
		modes[GameMode::Classic] = {"3 VS 3", "经典模式"};
		modes[GameMode::FourVsFour] = {"4 VS 4", ""};
		modes[GameMode::HardCore_4Vs4] = {"硬核模式 (4 VS 4)", "禁用装备"};
		modes[GameMode::Boss] = {"Boss模式 (3 VS 1)", ""};
		modes[GameMode::Duels] = {"决斗模式 (1 VS 1)", ""};
		modes[GameMode::Arcade] = {"街机模式 (无尽 1 VS 1)", ""};
		modes[GameMode::Clone] = {"克隆模式 (3 VS 3)", ""};
		modes[GameMode::Deathmatch] = {"死亡竞赛 (3 VS 3)", ""};
		modes[GameMode::RandomDeathmatch] = {"随机死亡竞赛 (3 VS 3)", ""};
	}
	else // English
	{
		modes[GameMode::OneVsOne] = {"1 VS 1", ""};
		modes[GameMode::Classic] = {"3 VS 3", "Classic Mode"};
		modes[GameMode::FourVsFour] = {"4 VS 4", ""};
		modes[GameMode::HardCore_4Vs4] = {"HardCore (4 VS 4)", "Disabled gear"};
		modes[GameMode::Boss] = {"Boss (3 VS 3)", ""};
		modes[GameMode::Duels] = {"Duels (1 VS 1 Arena)", ""};
		modes[GameMode::Arcade] = {"Arcade (Endless 1 VS 1)", ""};
		modes[GameMode::Clone] = {"Clone (3 VS 3)", ""};
		modes[GameMode::Deathmatch] = {"Deathmatch (3 VS 3)", ""};
		modes[GameMode::RandomDeathmatch] = {"Random Deathmatch (3 VS 3)", ""};
	}

	// Boss and Deathmatch are still in development and have no menu slot
	// right now (Duels and Arcade are using theirs), so nothing is locked here.

	// init mode handlers
	for (size_t i = 0; i < GameMode::__Internal_Max_Length; i++)
	{
		auto &data = modes.at(i);
		if (data.isLocked)
			data.description += " (In developtment)";
		// data.handler = s_ModeHandlers[i];
	}
}

bool GameModeLayer::pushMode(const GameModeData &data)
{
	return true;
}

void GameModeLayer::removeMode(const GameModeData &data)
{
}

void GameModeLayer::selectMode(GameMode mode)
{
	auto data = modes[(size_t)mode];
	string label = data.title;
	if (data.description.size() > 0)
	{
		label += " | ";
		label += data.description;
	}
	menuLabel->setString(label.c_str());

	if (setSelect(mode))
	{
		for (size_t i = 0; i < modes.size(); i++)
			modes.at(i).isLocked = true;
		CCLOG("Selected %s mode", data.title.c_str());

		// A saved Arcade run asks whether to carry on before anything else.
		if (mode == GameMode::Arcade)
		{
			const int streak = KTools::readArcadeStreak();
			KTools::ArcadeTeam team;
			if (streak > 0 && KTools::readArcadeTeam(team))
			{
				_savedLeader = team.leader;
				_savedAlly1 = team.ally1;
				_savedAlly2 = team.ally2;
				showArcadeResumePrompt(streak);
				return;
			}
		}

		enterMode(mode);
	}
}

void GameModeLayer::enterMode(GameMode mode)
{
	s_GameMode = mode;
	bool enableCustomSelect = false;
	if (Cheats < kMaxCheats && (mode == GameMode::FourVsFour || mode == GameMode::HardCore_4Vs4))
	{
		enableCustomSelect = false;
	}
	else if (mode != GameMode::RandomDeathmatch && mode != GameMode::Clone)
	{
		enableCustomSelect = Cheats >= kMaxCheats;
	}

	// call lua global function StartMenu.enterSelectLayer
	auto pStack = get_luastack;
	auto state = pStack->getLuaState();
	lua_getglobal(state, UiFlowKeys::kEnterSelectLayer);
	pStack->pushInt(mode);
	pStack->pushBoolean(enableCustomSelect);
	pStack->executeFunction(2);

	auto handler = getGameModeHandler();
	handler->setOldCheats(Cheats);
	handler->init();
}

void GameModeLayer::showArcadeResumePrompt(int streak)
{
	if (arcadePromptLayer)
		return;

	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/select.ogg");
	arcadePromptLayer = Layer::create();

	auto bg = Sprite::createWithSpriteFrameName("confirm_bg.png");
	bg->setPosition(Vec2(winSize.width / 2, winSize.height / 2));

	// Dedicated title/text art is optional; BMFont labels stand in without it.
	auto frames = SpriteFrameCache::sharedSpriteFrameCache();
	Node *title = nullptr;
	if (auto frame = frames->spriteFrameByName("arcade_title.png"))
	{
		title = Sprite::createWithSpriteFrame(frame);
	}
	else
	{
		auto label = CCLabelBMFont::create("ARCADE", Fonts::Default);
		label->setScale(0.5f);
		title = label;
	}
	title->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 38));

	Node *text = nullptr;
	if (auto frame = frames->spriteFrameByName("arcade_text.png"))
	{
		text = Sprite::createWithSpriteFrame(frame);
	}
	else
	{
		auto label = CCLabelBMFont::create(format("Continue your run? (Stage {})", streak + 1).c_str(), Fonts::Default,
										   bg->getContentSize().width - 20, kCCTextAlignmentCenter);
		label->setScale(0.4f);
		text = label;
	}
	text->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 8));

	MenuItem *yes_btn = MenuItemSprite::create(Sprite::createWithSpriteFrameName("yes_btn1.png"), Sprite::createWithSpriteFrameName("yes_btn2.png"), this, menu_selector(GameModeLayer::onArcadeResumeYes));
	MenuItem *no_btn = MenuItemSprite::create(Sprite::createWithSpriteFrameName("no_btn1.png"), Sprite::createWithSpriteFrameName("no_btn2.png"), this, menu_selector(GameModeLayer::onArcadeResumeNo));

	Menu *menu = Menu::create(yes_btn, no_btn, nullptr);
	menu->alignItemsHorizontallyWithPadding(24);
	menu->setPosition(Vec2(winSize.width / 2, winSize.height / 2 - 30));

	arcadePromptLayer->addChild(bg, 1);
	arcadePromptLayer->addChild(menu, 2);
	arcadePromptLayer->addChild(title, 2);
	arcadePromptLayer->addChild(text, 2);
	addChild(arcadePromptLayer, 500);
}

void GameModeLayer::onArcadeResumeYes(Ref *sender)
{
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
	arcadePromptLayer->removeFromParent();
	arcadePromptLayer = nullptr;

	s_GameMode = GameMode::Arcade;
	auto handler = getGameModeHandler();
	handler->setOldCheats(Cheats);
	handler->init();

	// The select screen is skipped, but it is also what loads these atlases;
	// load the same set so nothing later reaches for a frame that was never
	// brought in.
	addSprites("Record.plist");
	addSprites("Record2.plist");
	addSprites("Select.plist");
	addSprites("UI.plist");
	addSprites("Report.plist");
	addSprites("Ougis.plist");
	addSprites("Ougis2.plist");
	addSprites("Map.plist");
	addSprites("Gears.plist");

	auto tempSelect = SelectLayer::create();
	tempSelect->setSelectHero(_savedLeader.c_str());
	if (!_savedAlly1.empty())
		tempSelect->setCom1Select(_savedAlly1.c_str());
	if (!_savedAlly2.empty())
		tempSelect->setCom2Select(_savedAlly2.c_str());

	// onInitHeros() copies what it needs straight away, so the short-lived
	// layer and the strings above only have to live through this call.
	handler->selectLayer = tempSelect;
	handler->onInitHeros();

	auto loadScene = Scene::create();
	auto loadLayer = LoadLayer::create();
	loadLayer->preloadAudio();
	loadScene->addChild(loadLayer);

	Director::sharedDirector()->replaceScene(TransitionFade::create(1.0f, loadScene));
}

void GameModeLayer::onArcadeResumeNo(Ref *sender)
{
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/cancel.ogg");
	arcadePromptLayer->removeFromParent();
	arcadePromptLayer = nullptr;

	// A new run: the streak starts from zero, then on to the normal select.
	KTools::saveArcadeStreak(0);
	enterMode(GameMode::Arcade);
}

bool GameModeLayer::setSelect(GameMode mode)
{
	auto &data = modes.at(mode);
	if (!data.isLocked && data.hasSelected)
		return true;

	for (size_t i = 0; i < modes.size(); i++)
		modes.at(i).hasSelected = false;
	data.hasSelected = true;
	return false;
}
