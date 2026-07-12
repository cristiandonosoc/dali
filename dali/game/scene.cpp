#include <dali/game/scene.h>

#include <dali/core/memory.h>
#include <dali/game/file.h>

#include <yaml-cpp/yaml.h>

namespace kdk {

// The scene is just the deltas from a freshly generated grid: any tile that is path or holds
// content, plus the goal. PathDirection is derived (CalculatePath) and never stored.
bool SaveScene(const World& world, StringView path) {
    YAML::Emitter out;
    out << YAML::BeginMap;

    if (world.Goal.has_value()) {
        out << YAML::Key << "goal" << YAML::Value << YAML::Flow << YAML::BeginSeq << world.Goal->Q
            << world.Goal->R << YAML::EndSeq;
    }

    out << YAML::Key << "tiles" << YAML::Value << YAML::BeginSeq;
    for (const TileChunk& chunk : world.Grid.Chunks) {
        for (const Tile& tile : chunk.Tiles) {
            if (!tile.IsPath && tile.Content == ETileContent::None) {
                continue;
            }
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "q" << YAML::Value << tile.Hex.Q;
            out << YAML::Key << "r" << YAML::Value << tile.Hex.R;
            out << YAML::Key << "path" << YAML::Value << (tile.IsPath ? 1 : 0);
            out << YAML::Key << "content" << YAML::Value << (int)tile.Content;
            out << YAML::EndMap;
        }
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    return WriteFile(path, std::span<const u8>((const u8*)out.c_str(), out.size()));
}

bool LoadScene(World* world, StringView path) {
    auto scratch = Arena::GetScratch();
    FileContents contents = ReadFile(scratch, path);
    if (!contents.IsValid()) {
        return false;  // no scene on disk yet
    }

    world->Grid.Init();
    world->Goal = {};
    world->Enemies.Clear();
    world->Projectiles.Clear();
    world->BaseHealth = World::kMaxBaseHealth;
    world->Gold = 0;

    // We build without exceptions, so use the non-throwing as<T>(fallback) accessors — as<T>()
    // throws on a missing/mistyped field. The file is our own throwaway output, so anything
    // unexpected just resolves to defaults rather than a crash.
    YAML::Node root = YAML::Load((const char*)contents.Data.data());

    if (root["goal"] && root["goal"].size() == 2) {
        world->Goal = Hex{root["goal"][0].as<int>(0), root["goal"][1].as<int>(0)};
    }
    if (root["tiles"]) {
        for (const auto& node : root["tiles"]) {
            Hex hex{node["q"].as<int>(0), node["r"].as<int>(0)};
            Tile* tile = world->Grid.FindTile(hex);
            if (!tile) {
                continue;
            }
            tile->IsPath = node["path"].as<int>(0) != 0;

            ETileContent content = (ETileContent)node["content"].as<int>(0);
            if (content == ETileContent::Spawner) {
                MakeSpawner(tile);  // re-seed the random phase offset on load
            } else if (content == ETileContent::Tower) {
                MakeTower(tile);
            } else {
                tile->Content = content;
            }
        }
    }

    world->CalculatePath();
    return true;
}

}  // namespace kdk
