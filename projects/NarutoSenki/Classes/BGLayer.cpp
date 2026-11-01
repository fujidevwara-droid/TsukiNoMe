#include "BGLayer.h"

void BGLayer::initBg(int mapId, bool isDuel)
{
	bgMap = isDuel ? Sprite::create(format("Maps/Duels/map_bg{}.png", mapId).c_str())
				   : Sprite::create(GetMapBgPath(mapId));
	bgMap->setAnchorPoint(Vec2(0, 0));

	if (!isDuel)
	{
		bgMap->setPosition(Vec2(0, 192));
		addChild(bgMap);
		return;
	}

	// The arena camera never scrolls, so the image is simply placed once:
	//  - centred horizontally, instead of its left edge on the screen edge;
	//  - top-aligned, which is where the normal 710x128 strip ends up too;
	//  - scaled up to the screen width if it is narrower, so there is never a
	//    black edge on either side.
	// It does not follow the map: the map's offset would slide the image off
	// the screen (and leave black where it ends).
	const Size image = bgMap->getContentSize();
	const float scale = image.width < winSize.width ? winSize.width / image.width : 1.0f;
	bgMap->setScale(scale);
	bgMap->setPosition(Vec2((winSize.width - image.width * scale) / 2.0f, winSize.height - image.height * scale));
	addChild(bgMap);
}
