#pragma once

namespace kdk {

struct PlatformState;

struct World {
	int Count = 0;
};

struct GameState {
	World World = {};
	int InternalDetectedReload = 0;
};

GameState* GetGameState(PlatformState* ps);
World* GetWorld(PlatformState* ps);


} // namespace kdk
