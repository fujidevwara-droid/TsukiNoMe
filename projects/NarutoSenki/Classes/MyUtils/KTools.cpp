#include "KTools.h"
#include "MyUtils/CMD5Checksum.h"
#include "MyUtils/UnlockRequirements.hpp"
#include <set>
#include "cocos-ext.h"
#include "Utils/Cocos2dxHelper.hpp"
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "../../../cocos2dx/platform/android/jni/JniHelper.h"
#endif

bool KTools::readXMLToArray(const string &filePath, CCArray *&array)
{
	unsigned long nSize;
	auto data = (const char *)FileUtils::sharedFileUtils()->getFileData(filePath.c_str(), "r", &nSize);
	if (data == nullptr)
	{
		CCMessageBox(format("Data {} is null", filePath).c_str(), "Read XML Error");
		return false;
	}
	tinyxml2::XMLDocument doc;
	auto err = doc.Parse(data, nSize);
	delete data;
	if (err)
	{
		CCMessageBox(format("XML Error: {}", tinyxml2::XMLDocument::ErrorIDToName(err)).c_str(), "Parse XML Error");
		return false;
	}

	auto rootEle = doc.RootElement();
	auto actionEle = rootEle->FirstChildElement();

	while (actionEle)
	{
		// const char *actionName = actionEle->FirstAttribute()->Value();
		// CCLog("[%s]",actionName);

		auto actionNodeEle = actionEle->FirstChildElement();
		auto actionArray = CCArray::create();
		auto actionData = CCArray::create();
		auto actionFrame = CCArray::create();
		while (actionNodeEle)
		{
			// CCLog("+[%s]",actionNodeEle->Name());
			if (!is_same(actionNodeEle->Name(), "frame"))
			{
				auto dataEle = actionNodeEle->FirstChildElement();

				while (dataEle)
				{
					const char *nodeKey = dataEle->FirstAttribute()->Value();
					CCString *nodeValue = CCString::create(dataEle->GetText());

					// CCLog("%s:%s",nodeKey,nodeValue->getCString());
					auto dataDic = CCDictionary::create();
					dataDic->setObject(nodeValue, nodeKey);

					actionData->addObject(dataDic);
					dataEle = dataEle->NextSiblingElement();
				}
			}
			else
			{
				auto frameEle = actionNodeEle->FirstChildElement();

				while (frameEle)
				{
					const char *nodeKey;
					if (is_same(frameEle->Name(), "f"))
						nodeKey = frameEle->Name();
					else
						nodeKey = frameEle->FirstAttribute()->Value();

					CCString *nodeValue = CCString::create(frameEle->GetText());

					// CCLog("%s:%s",nodeKey,nodeValue->getCString());
					auto frameDic = CCDictionary::create();
					frameDic->setObject(nodeValue, nodeKey);
					actionFrame->addObject(frameDic);

					frameEle = frameEle->NextSiblingElement();
				}
			}
			actionNodeEle = actionNodeEle->NextSiblingElement();
		}
		actionArray->addObject(actionData);
		actionArray->addObject(actionFrame);

		array->addObject(actionArray);
		actionEle = actionEle->NextSiblingElement();
	}

	return true;
}

int isExisted(void *para, int n_column, char **column_value, char **column_name)
{
	bool *isExisted_ = (bool *)para;
	*isExisted_ = (**column_value) != '0';
	return 0;
}

void KTools::encode(string &str, int radomKey)
{
	for (size_t i = 0; i < str.length(); i++)
	{
		int ch = str[i];
		ch += radomKey;
		str[i] = ch;
	}
	str += radomKey;
}

void KTools::decode(string &str)
{
	size_t num = str.length() - 1;
	int key = str[num];
	str = str.substr(0, num);

	for (size_t i = 0; i < str.length(); i++)
	{
		int ch = str[i];
		ch -= key;
		str[i] = ch;
	}
}

void KTools::initTableInDB()
{
	sqlite3 *pDB = nullptr;
	char *errorMsg = nullptr;
	string path = FileUtils::sharedFileUtils()->getWritablePath() + "sql.db";
	int result = sqlite3_open(path.c_str(), &pDB);
	if (result != SQLITE_OK)
	{
		CCLOG("open sql file Failed!");
		return;
	}

	string sql = "drop table IF EXISTS Achievement";

	sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

	sql = "create table if not exists CharRecord (name char(20)  primary key ,column1 char(10),column2 char(10),column3 char(10))";
	sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

	sql = "select * from  GameRecord";

	sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

	if (errorMsg != nullptr)
	{
		errorMsg = nullptr;
		sql = "create table if not exists GameRecord (id char(10) primary key,coin char(20),version char(20))";
		sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

		auto coin = "n>";
		sql = format("insert into GameRecord values(1,'{}','1')", coin);
		int result = sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

		if (errorMsg != nullptr)
		{
			CCLOG("exec sql %s failed with mgs: %s", sql.c_str(), errorMsg);
			sqlite3_close(pDB);
			return;
		}
	}

	bool isExisted_;

	sql = "select count(*) from  CharRecord";
	sqlite3_exec(pDB, sql.c_str(), isExisted, &isExisted_, &errorMsg);

	(void)isExisted_;

	// Insert a CharRecord row for every hero in kHeroList that has none yet.
	// On a fresh install that is all of them; on an existing save it is only
	// the heroes added since (names are stored encoded, so decode to compare).
	{
		std::set<string> existingNames;
		char **nameResult = nullptr;
		int nameRows = 0;
		int nameCols = 0;
		sql = "select name from CharRecord";
		sqlite3_get_table(pDB, sql.c_str(), &nameResult, &nameRows, &nameCols, nullptr);
		if (nameResult != nullptr)
		{
			for (int r = 1; r <= nameRows; ++r)
			{
				if (nameResult[r] == nullptr)
					continue;
				string stored = nameResult[r];
				if (stored.empty())
					continue;
				decode(stored);
				existingNames.insert(stored);
			}
			sqlite3_free_table(nameResult);
		}

		for (size_t i = 0; i < kHeroNum; i++)
		{
			if (existingNames.count(kHeroList[i]))
				continue;

			string name = kHeroList[i];
			int key = rand() % 50 + 40;
			encode(name, key);

			string column1DB = "0";
			key = rand() % 60 + 40;
			encode(column1DB, key);

			string column2DB = "0";
			key = rand() % 60 + 40;
			encode(column2DB, key);

			string column3DB = "";
			key = rand() % 60 + 40;
			encode(column3DB, key);

			sql = format("insert into  CharRecord values('{}','{}','{}','{}')", name, column1DB, column2DB, column3DB);
			sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

			if (errorMsg != nullptr)
			{
				CCLOG("exec sql %s failed with mgs: %s", sql.c_str(), errorMsg);
				sqlite3_close(pDB);
				return;
			}
		}
	}

	sqlite3_close(pDB);
}

void KTools::initColumeInDB()
{
	sqlite3 *pDB = nullptr;
	char *errorMsg = nullptr;
	string path = FileUtils::sharedFileUtils()->getWritablePath() + "sql.db";
	int result = sqlite3_open(path.c_str(), &pDB);
	if (result != SQLITE_OK)
	{
		CCLOG("open sql file Failed!");
		return;
	}

	char **result2;
	int row = 0;
	int column = 0;

	string sql = "select coin from GameRecord";
	sqlite3_get_table(pDB, sql.c_str(), &result2, &row, &column, nullptr);
	string str2 = result2[1];
	decode(str2);

	string str3 = "uuuuu<";
	decode(str3);
	if (to_int(str2.c_str()) > to_int(str3.c_str()))
	{
		auto coin = "uuuuu<";
		sql = format("update GameRecord set coin='{}'", coin);
		sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);
	}
}

void KTools::prepareFileOGG(const string &listName, bool unload /* =false */)
{
	if (!UserDefault::sharedUserDefault()->getBoolForKey("isPreload"))
	{
		return;
	}

	// MD5
	const string md5Path = "Audio/list.xml";
	bool isExisted = FileUtils::sharedFileUtils()->isFileExist(md5Path);
	if (!isExisted)
	{
		return;
	}

	unsigned long nSize;
	const char *pXmlBuffer = (const char *)FileUtils::sharedFileUtils()->getFileData(md5Path.c_str(), "r", &nSize);
	tinyxml2::XMLDocument doc;
	doc.Parse(pXmlBuffer);
	delete pXmlBuffer;

	auto rootEle = doc.RootElement();
	auto fileEle = rootEle->FirstChildElement();

	while (fileEle)
	{
		auto pathSrc = fileEle->FirstAttribute()->Value();
		auto soundPath = fileEle->GetText();
		if (pathSrc == listName)
		{
			if (!unload)
			{
				SimpleAudioEngine::sharedEngine()->preloadEffect(soundPath);
			}
			else if (unload)
			{
				SimpleAudioEngine::sharedEngine()->unloadEffect(soundPath);
			}
		}

		fileEle = fileEle->NextSiblingElement();
	}
}

sqlite3 *KTools::prepareTableInDB()
{
	sqlite3 *pDB = nullptr;

	string path = FileUtils::sharedFileUtils()->getWritablePath() + "sql.db";
	int result = sqlite3_open(path.c_str(), &pDB);

	if (result != SQLITE_OK)
	{
		CCMessageBox(path.c_str(), "open sql file Failed!");
		return nullptr;
	}

	return pDB;
}

bool KTools::saveToSQLite(const char *table /* ="GameRecord"*/, const char *column /* =nullptr */, const char *value /* =nullptr */, bool isBuy /*= false */)
{
	sqlite3 *pDB = prepareTableInDB();
	char *errorMsg = nullptr;
	if (pDB != nullptr)
	{
		int key = rand() % 60 + 40;
		string coin = value;
		encode(coin, key);

		string sql = format("update {} set {}='{}'", table, column, coin);
		sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, &errorMsg);

		if (errorMsg != nullptr)
		{
			sqlite3_close(pDB);
			return false;
		}

		return true;
	}

	sqlite3_close(pDB);
	return false;
}

string KTools::readFromSQLite(const char *table /* ="GameRecord" */, const char *column /* =nullptr */, const char *value /* =nullptr */)
{
	sqlite3 *pDB = prepareTableInDB();

	if (pDB != nullptr)
	{
		char **result;

		string sql = format("select {} from  GameRecord", "coin");
		int row = 0;
		int column = 0;

		sqlite3_get_table(pDB, sql.c_str(), &result, &row, &column, nullptr);

		string str = result[1];
		decode(str);
		sqlite3_free_table(result);
		sqlite3_close(pDB);
		return str;
	}
	sqlite3_close(pDB);
	return "";
}

// Locates the row whose decoded key column equals `value` in a two-column
// result set from sqlite3_get_table (cell [0..1] are the headers). Returns the
// 1-based row index, or 0 when there is no match. NULL cells are skipped.
static int findKeyedRow(char **result, int rows, const char *value)
{
	for (int r = 1; r <= rows; ++r)
	{
		const char *cell = result[r * 2];
		if (cell == nullptr)
			continue;

		string key = cell;
		KTools::decode(key);
		if (key == value)
			return r;
	}
	return 0;
}

string KTools::readSQLite(const char *table, const char *column, const char *value, const char *targetColumn)
{
	sqlite3 *pDB = prepareTableInDB();
	if (pDB == nullptr)
		return "";

	char **result = nullptr;
	int rows = 0;
	int cols = 0;
	string sql = format("select {},{} from {}", column, targetColumn, table);
	sqlite3_get_table(pDB, sql.c_str(), &result, &rows, &cols, nullptr);

	string target;
	if (result != nullptr)
	{
		int r = findKeyedRow(result, rows, value);
		// A NULL cell is a column that was added after the row was created.
		if (r > 0 && result[r * 2 + 1] != nullptr)
		{
			target = result[r * 2 + 1];
			decode(target);

			// Numeric columns are normalised; free-text ones (times) are not.
			// Normalising only a row that was actually found keeps "no row"
			// (empty) distinguishable from "row holding 0".
			if (!is_same(targetColumn, "column3") &&
				!is_same(targetColumn, "column4") &&
				!is_same(targetColumn, "duel_time"))
			{
				target = std::to_string(to_int(target.c_str()));
			}
		}
		sqlite3_free_table(result);
	}

	sqlite3_close(pDB);
	return target;
}

void KTools::saveSQLite(const char *table, const char *relatedColumn, const char *value, const char *targetColumn, const string &targetValue, bool isPlus)
{
	sqlite3 *pDB = prepareTableInDB();
	if (pDB == nullptr)
		return;

	char **result = nullptr;
	int rows = 0;
	int cols = 0;
	string sql = format("select {},{} from {}", relatedColumn, targetColumn, table);
	sqlite3_get_table(pDB, sql.c_str(), &result, &rows, &cols, nullptr);

	if (result != nullptr)
	{
		int r = findKeyedRow(result, rows, value);
		if (r > 0)
		{
			// The WHERE clause needs the key exactly as stored (still encoded).
			string storedKey = result[r * 2];

			string current;
			if (result[r * 2 + 1] != nullptr)
			{
				current = result[r * 2 + 1];
				decode(current);
			}

			string saveValue = isPlus ? targetValue + current : targetValue;
			encode(saveValue, rand() % 50 + 40);

			sql = format("update {} set {}='{}' where {}='{}'", table, targetColumn, saveValue, relatedColumn, storedKey);
			sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, nullptr);
		}
		sqlite3_free_table(result);
	}

	if (isPlus)
	{
		// Clamp the coin balance to its cap.
		char **coinResult = nullptr;
		int coinRows = 0;
		int coinCols = 0;
		sql = "select coin from GameRecord";
		sqlite3_get_table(pDB, sql.c_str(), &coinResult, &coinRows, &coinCols, nullptr);
		if (coinResult != nullptr)
		{
			if (coinRows >= 1 && coinResult[1] != nullptr)
			{
				string balance = coinResult[1];
				decode(balance);

				string cap = "uuuuu<";
				decode(cap);

				if (to_int(balance.c_str()) > to_int(cap.c_str()))
				{
					sql = "update GameRecord set coin='uuuuu<'";
					sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, nullptr);
				}
			}
			sqlite3_free_table(coinResult);
		}
	}

	sqlite3_close(pDB);
}

int KTools::readWinNumFromSQL(const char *heroName)
{
	return to_int(readSQLite("CharRecord", "name", heroName, "column1").c_str());
}

int KTools::readCoinFromSQL()
{
	auto coins = readFromSQLite();
	return to_int(coins.c_str());
}

const char *KTools::readRecordTimeFromSQL(const char *heroName)
{
	// Static so the pointer outlives the call (the old version returned the
	// buffer of a temporary string).
	static string recordTime;
	recordTime = readSQLite("CharRecord", "name", heroName, "column3");
	return recordTime.c_str();
}

// ---------------------------------------------------------------------------
// Duels / Arcade persistence
// ---------------------------------------------------------------------------

// SQLite has no "add column if missing". The first attempt adds it, later ones
// fail harmlessly, so each column is only attempted once per session.
static void ensureColumn(const char *table, const char *column, const char *type)
{
	static std::set<string> attempted;
	string id = format("{}.{}", table, column);
	if (!attempted.insert(id).second)
		return;

	sqlite3 *pDB = KTools::prepareTableInDB();
	if (pDB == nullptr)
	{
		attempted.erase(id);
		return;
	}
	string sql = format("alter table {} add column {} {}", table, column, type);
	sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, nullptr);
	sqlite3_close(pDB);
}

// Reads the first row of a single GameRecord column and decodes it.
static bool readGameRecordCell(const char *column, string &out)
{
	sqlite3 *pDB = KTools::prepareTableInDB();
	if (pDB == nullptr)
		return false;

	char **result = nullptr;
	int rows = 0;
	int cols = 0;
	string sql = format("select {} from GameRecord", column);
	sqlite3_get_table(pDB, sql.c_str(), &result, &rows, &cols, nullptr);

	bool found = false;
	if (result != nullptr)
	{
		if (rows >= 1 && result[1] != nullptr)
		{
			out = result[1];
			KTools::decode(out);
			found = true;
		}
		sqlite3_free_table(result);
	}
	sqlite3_close(pDB);
	return found;
}

static void writeGameRecordCell(const char *column, string value)
{
	sqlite3 *pDB = KTools::prepareTableInDB();
	if (pDB == nullptr)
		return;

	KTools::encode(value, rand() % 60 + 40);
	string sql = format("update GameRecord set {}='{}'", column, value);
	sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, nullptr);
	sqlite3_close(pDB);
}

string KTools::readDuelBestTime(const char *heroName)
{
	ensureColumn("CharRecord", "duel_time", "char(10)");
	return readSQLite("CharRecord", "name", heroName, "duel_time");
}

void KTools::saveDuelBestTime(const char *heroName, const string &time)
{
	ensureColumn("CharRecord", "duel_time", "char(10)");
	saveSQLite("CharRecord", "name", heroName, "duel_time", time, false);
}

int KTools::readArcadeStreak()
{
	ensureColumn("GameRecord", "streak", "char(20)");
	string raw;
	return readGameRecordCell("streak", raw) ? to_int(raw.c_str()) : 0;
}

void KTools::saveArcadeStreak(int streak)
{
	ensureColumn("GameRecord", "streak", "char(20)");
	writeGameRecordCell("streak", std::to_string(streak));
}

int KTools::readArcadeBest(const char *heroName)
{
	ensureColumn("CharRecord", "arcade_record", "char(20)");
	return to_int(readSQLite("CharRecord", "name", heroName, "arcade_record").c_str());
}

void KTools::saveArcadeBestIfHigher(const char *heroName, int stage)
{
	if (stage <= readArcadeBest(heroName))
		return;
	saveSQLite("CharRecord", "name", heroName, "arcade_record", std::to_string(stage), false);
}

void KTools::saveArcadeTeam(const ArcadeTeam &team)
{
	ensureColumn("GameRecord", "dm_team", "char(64)");
	writeGameRecordCell("dm_team", team.leader + "|" + team.ally1 + "|" + team.ally2);
}

bool KTools::readArcadeTeam(ArcadeTeam &team)
{
	ensureColumn("GameRecord", "dm_team", "char(64)");

	string raw;
	if (!readGameRecordCell("dm_team", raw))
		return false;

	// "leader|ally1|ally2" - the allies may be empty.
	string parts[3];
	size_t start = 0;
	for (int i = 0; i < 3; ++i)
	{
		size_t bar = (i < 2) ? raw.find('|', start) : string::npos;
		parts[i] = raw.substr(start, bar == string::npos ? string::npos : bar - start);
		if (bar == string::npos)
			break;
		start = bar + 1;
	}

	team = { parts[0], parts[1], parts[2] };
	return !team.leader.empty();
}

static void ensureUnlockTable()
{
	static bool created = false;
	if (created)
		return;

	sqlite3 *pDB = KTools::prepareTableInDB();
	if (pDB == nullptr)
		return;
	sqlite3_exec(pDB, "create table if not exists UnlockedChar (name char(20) primary key, unlocked char(10))", nullptr, nullptr, nullptr);
	sqlite3_close(pDB);
	created = true;
}

static void setUnlockRow(const char *heroName, const char *flag)
{
	ensureUnlockTable();

	if (!KTools::readSQLite("UnlockedChar", "name", heroName, "unlocked").empty())
	{
		KTools::saveSQLite("UnlockedChar", "name", heroName, "unlocked", flag, false);
		return;
	}

	sqlite3 *pDB = KTools::prepareTableInDB();
	if (pDB == nullptr)
		return;

	string name = heroName;
	KTools::encode(name, rand() % 50 + 40);
	string state = flag;
	KTools::encode(state, rand() % 60 + 40);

	string sql = format("insert into UnlockedChar values('{}','{}')", name, state);
	sqlite3_exec(pDB, sql.c_str(), nullptr, nullptr, nullptr);
	sqlite3_close(pDB);
}

bool KTools::hasUnlockRecord(const char *heroName)
{
	ensureUnlockTable();
	return !readSQLite("UnlockedChar", "name", heroName, "unlocked").empty();
}

bool KTools::isCharacterUnlocked(const char *heroName)
{
	ensureUnlockTable();
	string state = readSQLite("UnlockedChar", "name", heroName, "unlocked");
	return state.empty() || to_int(state.c_str()) != 0;
}

void KTools::unlockCharacter(const char *heroName)
{
	setUnlockRow(heroName, "1");
}

void KTools::lockCharacter(const char *heroName)
{
	setUnlockRow(heroName, "0");
}

bool KTools::isHeroUnlocked(const char *heroName)
{
	return Unlocks::isUnlocked(heroName);
}

bool KTools::isHeroUnlockEligible(const char *heroName)
{
	return Unlocks::isEligible(heroName);
}

bool KTools::tryUnlockHeroViaPurchase(const char *heroName)
{
	return Unlocks::tryPurchase(heroName);
}

string KTools::getUnlockConditionLines(const char *heroName)
{
	return Unlocks::describeJoined(heroName);
}

string KTools::encodeData(string data)
{
	// SHA1 *sha1;
	// unsigned char *digest;
	string dataMD5 = CMD5Checksum::GetMD5OfString(data);

	// sha1=new SHA1;
	////sha1->addBytes( "goldlion" ,strlen( "goldlion" ));//
	// string result=(char*) digest;
	// delete sha1;
	// free(digest);
	return dataMD5;
}

bool CCTips::init(const char *tips)
{
	RETURN_FALSE_IF(!Sprite::init());

	setAnchorPoint(Vec2(0.5, 0.5));
	auto strings = CCDictionary::createWithContentsOfFile("Config/strings.xml");
	const char *reply = ((CCString *)strings->objectForKey(tips))->m_sString.c_str();

	CCLabelTTF *tipLabel = CCLabelTTF::create(reply, FONT_NAME, 12);
	addChild(tipLabel, 5000);
	setPosition(Vec2(winSize.width / 2, 50));
	auto call = CallFunc::create(std::bind(&CCTips::onDestroy, this));

	auto mv = MoveBy::create(0.2f, Vec2(0, 12));
	auto fadeOut = FadeOut::create(0.2f);
	auto delay = DelayTime::create(2.0f);
	auto sp = Spawn::create(fadeOut, mv, nullptr);
	auto seq = newSequence(sp, delay, call);
	runAction(seq);

	return true;
}

void CCTips::onDestroy()
{
	removeAllChildren();
	removeFromParent();
}

CCTips *CCTips::create(const char *tips)
{
	CCTips *ab = new CCTips();
	if (ab && ab->init(tips))
	{
		ab->autorelease();
		return ab;
	}
	else
	{
		delete ab;
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// Greyscale
// ---------------------------------------------------------------------------

// Luminance-weighted grey. Alpha and the vertex colour (opacity, tint) are kept,
// so a faded or tinted node still behaves normally while it is grey.
static const GLchar *kGrayscaleFragment =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"varying vec2 v_texCoord;\n"
	"varying vec4 v_fragmentColor;\n"
	"uniform sampler2D CC_Texture0;\n"
	"void main()\n"
	"{\n"
	"    vec4 texel = texture2D(CC_Texture0, v_texCoord);\n"
	"    float grey = dot(texel.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    gl_FragColor = vec4(vec3(grey), texel.a) * v_fragmentColor;\n"
	"}\n";

static const char *kGrayscaleProgramKey = "NS_ShaderGrayscale";

// Built on first use and kept in the shader cache. Looking it up there each
// time (instead of holding a raw pointer) means a cache purge or a rebuilt GL
// context just makes the next call compile it again.
static CCGLProgram *getGrayscaleProgram()
{
	auto cache = CCShaderCache::sharedShaderCache();
	if (auto program = cache->programForKey(kGrayscaleProgramKey))
		return program;

	auto program = new CCGLProgram();
	if (!program->initWithVertexShaderByteArray(ccPositionTextureColor_vert, kGrayscaleFragment))
	{
		program->release();
		return nullptr;
	}

	program->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
	program->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
	program->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
	program->link();
	program->updateUniforms();

	cache->addProgram(program, kGrayscaleProgramKey); // the cache keeps its own reference
	program->release();
	return program;
}

void KTools::setNodeGrayscale(CCNode *node, bool enable)
{
	if (node == nullptr)
		return;

	CCGLProgram *program = enable
		? getGrayscaleProgram()
		: CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor);

	// A failed compile leaves the node as it was rather than blank.
	if (program == nullptr)
		return;

	node->setShaderProgram(program);

	// A Button swaps between its normal, pressed and disabled renderers, which
	// are sibling sprites under the button. Holding or clicking it shows the
	// pressed one, so the program has to be on all of them or a greyscale
	// portrait turns colour for as long as it is pressed.
	if (CCNode *parent = node->getParent())
	{
		if (dynamic_cast<ui::Widget *>(parent))
		{
			CCObject *child = nullptr;
			CCARRAY_FOREACH(parent->getChildren(), child)
			{
				if (auto sprite = dynamic_cast<CCSprite *>(child))
					sprite->setShaderProgram(program);
			}
		}
	}
}
