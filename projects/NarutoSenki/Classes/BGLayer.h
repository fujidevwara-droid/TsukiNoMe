#pragma once
#include "GameLayer.h"

class BGLayer : public Layer
{
public:
	Sprite *bgMap = nullptr;

	// isDuel: the arena maps carry their own backgrounds, one per map, in the
	// same folder (Maps/Duels/map_bg<N>.png).
	void initBg(int mapId, bool isDuel = false);

	// Replaces the background image (a missing or empty file leaves no image). isDuelStyle
	// places it like an arena background: centred, top-aligned, scaled up to the screen
	// width. Otherwise like the strip of the normal maps.
	void setBackground(const string &file, bool isDuelStyle);

	CREATE_FUNC(BGLayer);
};
