#include <dali/game/scene.h>

#include <yaml-cpp/yaml.h>

#include <fstream>

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
    for (const Tile& tile : world.Grid.Tiles) {
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
    out << YAML::EndSeq;
    out << YAML::EndMap;

    std::ofstream file(path.Str());
    if (!file.good()) {
        return false;
    }
    file << out.c_str();
    return file.good();
}

bool LoadScene(World* world, StringView path) {
    std::ifstream file(path.Str());
    if (!file.good()) {
        return false;  // no scene on disk yet
    }

    world->Grid.InitRing(3);
    world->Goal = {};
    world->Enemies.Clear();
    world->SpawnTimer = 0.0f;

    try {
        YAML::Node root = YAML::Load(file);

        if (root["goal"] && root["goal"].size() == 2) {
            world->Goal = Hex{root["goal"][0].as<int>(), root["goal"][1].as<int>()};
        }
        if (root["tiles"]) {
            for (const auto& node : root["tiles"]) {
                Hex hex{node["q"].as<int>(), node["r"].as<int>()};
                Tile* tile = world->Grid.FindTile(hex);
                if (!tile) {
                    continue;
                }
                tile->IsPath = node["path"].as<int>() != 0;
                tile->Content = (ETileContent)node["content"].as<int>();
            }
        }
    } catch (const std::exception&) {
        return false;
    }

    world->CalculatePath();
    return true;
}

}  // namespace kdk
