#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "src/persistence/GameplaySessionSystem.hpp"

// Player-facing transactional slots. Raw session import/export remains available
// through GameplaySessionSystem for tools and historical fixtures.
class GameSaveStore {
public:
  explicit GameSaveStore(std::filesystem::path root):root_(std::move(root)) {}
  static std::filesystem::path defaultRoot();
  const std::filesystem::path& root() const { return root_; }
  struct Entry {
    std::string name;
    int backup=0;
    uint32_t tick=0;
    int width=0,height=0;
    bool valid=false;
  };
  std::vector<Entry> entries() const;
  static bool validName(const std::string& name);
  bool save(const std::string& name, const CityMap& map, const RoadNetwork& roads,
            const EntityStore& store, const PopulationStore& population,
            const GameplaySessionState& session, std::string* error=nullptr) const;
  // Fully validate before returning a generation; invalid saves never mutate a city.
  std::string resolve(const std::string& name, int backup=0, std::string* error=nullptr) const;
  std::string recover(const std::string& name, bool& recovered, std::string* error=nullptr) const;
private:
  std::filesystem::path root_;
};
