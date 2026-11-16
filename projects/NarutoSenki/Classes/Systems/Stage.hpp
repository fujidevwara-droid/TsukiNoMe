#pragma once
#include "Defines.h"

// A stage is one map the fighting can happen on. The main battle is stage 0; other
// stages (a trap map, a map change) sit beside it in the same world, far away along x,
// so that fighting on one never touches the other: anything that works by distance sees
// the other stage's units as very far away.
//
// Every unit knows which stage it is on (CharacterBase::_stageId; a unit spawned by
// another one starts on its stage), and everything that used to ask the one global map
// about edges, walls, base regions or the camera asks the unit's stage instead.
//
//  * originX is where the map's own (0, 0) corner is, in the GameLayer's coordinates.
//    Positions are always GameLayer coordinates; "stage x" is position - originX, which
//    is what tile-based map logic works in. The y origin is always 0 (stages are only
//    ever placed along x), so a y position is the same in both.
//  * arena stages run by the duel arena rules (walls, a fixed camera centred on the map,
//    no base to heal at, AI that fights what is in the arena), whatever the game mode.
//    The main stage is an arena exactly when the game mode is Duels or Arcade.
struct Stage
{
	int id = 0;
	std::string name;			// the map's name or number as it was written, "" for none
	TMXTiledMap *map = nullptr; // a child of the GameLayer; not owned here
	float originX = 0.0f;
	bool arena = false;
	std::string bgFile;			// the background image shown while this stage is watched, "" for none

	float tileWidth() const { return map->getTileSize().width; }
	float tileHeight() const { return map->getTileSize().height; }
	float tilesX() const { return map->getMapSize().width; }
	Size pixels() const
	{
		return Size(map->getMapSize().width * map->getTileSize().width,
					map->getMapSize().height * map->getTileSize().height);
	}
};

constexpr int kMainStage = 0;

// Stage n sits at x = n * kStageSpacing, so stages never overlap and anything that
// works by distance sees units on another stage as very far away.
constexpr float kStageSpacing = 100000.0f;
