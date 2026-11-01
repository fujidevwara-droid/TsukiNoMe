#pragma once
#include "Defines.h"
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "../../external/sqlite3/src/sqlite3.h"
#else
#include "sqlite3.h"
#endif

class KTools
{
public:
	static bool readXMLToArray(const string &filePath, CCArray *&array);
	static void prepareFileOGG(const string &listName, bool unload = false);

	static void initTableInDB();
	static void initColumeInDB();

	static sqlite3 *prepareTableInDB();

	static bool saveToSQLite(const char *table = "GameRecord", const char *column = nullptr, const char *value = nullptr, bool isBuy = false);
	static string readFromSQLite(const char *table = "GameRecord", const char *column = nullptr, const char *value = nullptr);

	static void encode(string &str, int randomKey);
	static void decode(string &str);

	static string readSQLite(const char *table, const char *column, const char *value, const char *targetColumn);
	static void saveSQLite(const char *table, const char *relatedColumn, const char *value, const char *targetColumn, const string &targetValue, bool isPlus);

	static int readWinNumFromSQL(const char *heroName);
	static int readCoinFromSQL();
	// Returned pointer stays valid until the next call (it points at a static buffer).
	static const char *readRecordTimeFromSQL(const char *heroName);

	// ---- Duels / Arcade persistence -------------------------------------
	// Every value below is stored through the same encode()/decode() scheme as
	// the rest of the save data. Extra columns are added to the existing
	// tables on first use, so older save files keep working untouched.

	// Best Duels win time per hero (CharRecord.duel_time).
	static string readDuelBestTime(const char *heroName);
	static void saveDuelBestTime(const char *heroName, const string &time);

	// Arcade: the live win streak that seeds the next run (GameRecord.streak).
	static int readArcadeStreak();
	static void saveArcadeStreak(int streak);

	// Arcade: highest stage each hero has ever cleared (CharRecord.arcade_record).
	// Only ever moves upward - a lost run never lowers it.
	static int readArcadeBest(const char *heroName);
	static void saveArcadeBestIfHigher(const char *heroName, int stage);

	// Arcade: the last team fielded, so a run can be resumed (GameRecord.dm_team).
	struct ArcadeTeam
	{
		string leader;
		string ally1;
		string ally2;
	};
	static void saveArcadeTeam(const ArcadeTeam &team);
	static bool readArcadeTeam(ArcadeTeam &team);

	// Character unlock state (UnlockedChar table). A hero with no row is
	// treated as unlocked - only heroes that have an unlock rule ever get one.
	static bool hasUnlockRecord(const char *heroName);
	static bool isCharacterUnlocked(const char *heroName);
	static void unlockCharacter(const char *heroName);
	static void lockCharacter(const char *heroName);

	// Rule-aware wrappers around MyUtils/UnlockRequirements.hpp. They exist
	// because the Lua binding generator can only expose class statics.
	static bool isHeroUnlocked(const char *heroName);
	static bool isHeroUnlockEligible(const char *heroName);
	static bool tryUnlockHeroViaPurchase(const char *heroName);
	// Newline-joined requirement text for the locked-hero popup.
	static string getUnlockConditionLines(const char *heroName);

	// Turns a greyscale shader on (enable = true) or off for one node.
	//
	// Safe on a single sprite frame taken from a shared atlas: the program is
	// applied when that node is drawn and only samples that node's own quad, so
	// the other frames of the atlas keep their colour. It does NOT work on a
	// sprite that is a child of a CCSpriteBatchNode, because a batch draws all
	// its children with one program and ignores the per-node override. The
	// portraits of the select screen are plain children, so they are fine.
	static void setNodeGrayscale(CCNode *node, bool enable);

	static string encodeData(string data);
};

class CCTips : public Sprite
{
public:
	bool init(const char *tips);

	static CCTips *create(const char *tips);

protected:
	void onDestroy();
};
