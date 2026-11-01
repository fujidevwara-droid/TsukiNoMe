#pragma once
#include "GameLayer.h"

class BGLayer : public Layer
{
public:
	Sprite *bgMap;

	// isDuel: the arena maps carry their own backgrounds, one per map, in the
	// same folder (Maps/Duels/map_bg<N>.png).
	void initBg(int mapId, bool isDuel = false);

	CREATE_FUNC(BGLayer);
};
