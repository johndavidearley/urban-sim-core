#include "src/persistence/GameSaveStore.hpp"
#include "src/core/EntityId.hpp"
#include "src/world/CityMap.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
using nlohmann::json;
std::mutex writer;
std::atomic<unsigned> sequence{0};
void error(std::string* out,const std::string& text) {if(out) *out=text;}
std::string read(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  if(!input) throw std::runtime_error("Cannot read "+path.string());
  return {std::istreambuf_iterator<char>(input),{}};
}
void syncFile(const std::filesystem::path& path) {
#ifdef _WIN32
  const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open save for synchronization");
  const bool ok=FlushFileBuffers(file)!=0;CloseHandle(file);
#else
  const int file=::open(path.c_str(),O_RDONLY);
  if(file<0) throw std::runtime_error("Cannot open save for synchronization");
  const bool ok=::fsync(file)==0;::close(file);
#endif
  if(!ok) throw std::runtime_error("Cannot synchronize save to disk");
}
void syncDirectory(const std::filesystem::path& path) {
#ifndef _WIN32
  const int dir=::open(path.c_str(),O_RDONLY);
  if(dir<0) throw std::runtime_error("Cannot open save directory");
  const bool ok=::fsync(dir)==0;::close(dir);
  if(!ok) throw std::runtime_error("Cannot synchronize save directory");
#else
  (void)path; // MoveFileExW below uses WRITE_THROUGH on Windows.
#endif
}
void write(const std::filesystem::path& path,const std::string& contents) {
  std::ofstream output(path,std::ios::binary|std::ios::trunc);
  output<<contents;output.flush();
  if(!output) throw std::runtime_error("Cannot write "+path.string());
  output.close();if(!output) throw std::runtime_error("Cannot close save file");
  syncFile(path);
}
void publish(const std::filesystem::path& path,const std::string& contents) {
  const auto temporary=std::filesystem::path(path.string()+".pending");
  write(temporary,contents);
#ifdef _WIN32
  if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot publish save; previous save retained");
#else
  std::filesystem::rename(temporary,path);
#endif
  syncDirectory(path.parent_path());
}
std::filesystem::path slot(const std::filesystem::path& root,const std::string& name,int backup) {
  return root/(name+(backup?".backup"+std::to_string(backup):"")+".json");
}
std::string generation(const json& manifest) {
  if(manifest.at("saveStoreVersion")!=1) throw std::runtime_error("Unsupported save-store version");
  const auto name=manifest.at("generation").get<std::string>();
  if(name.size()<3 || name.substr(0,2)!="g-" || name.find_first_not_of("g-0123456789")!=std::string::npos)
    throw std::runtime_error("Invalid save generation");
  return name;
}
void validate(const std::filesystem::path& path) {
  // Full-world restoration sets the process-wide allocator. Validation in a
  // temporary city must never rewind the allocator of the live city.
  struct PreserveAllocator {
    uint64_t next=EntityIdUtils::nextEntityId();
    ~PreserveAllocator() {EntityIdUtils::restoreNextEntityId(next);}
  } preserveAllocator;
  const auto document=json::parse(read(path));
  if(document.value("citySnapshot",std::string{})!="session.json.city.json" ||
     !std::filesystem::is_regular_file(path.parent_path()/"session.json.city.json"))
    throw std::runtime_error("Incomplete save generation or invalid core reference");
  int width=0,height=0;std::string reason;
  if(!GameplaySessionSystem::readMapDimensions(path.string(),width,height,&reason)) throw std::runtime_error(reason);
  CityMap map({width,height});RoadNetwork roads(map);EntityStore entities;PopulationStore people;GameplaySessionState session;
  if(!GameplaySessionSystem::load(path.string(),map,roads,entities,people,session,&reason)) throw std::runtime_error(reason);
}
// Only reclaim objects when every retained manifest is readable. Orphaned
// staging generations from an interrupted write are safe to discard next save.
void collect(const std::filesystem::path& root) {
  try {
    std::set<std::string> retained;
    for(const auto& entry:std::filesystem::directory_iterator(root))
      if(entry.path().extension()==".json") retained.insert(generation(json::parse(read(entry.path()))));
    for(const auto& entry:std::filesystem::directory_iterator(root/"objects")) {
      const auto name=entry.path().filename().string();
      if(entry.is_directory() && name.substr(0,2)=="g-" && !retained.count(name)) std::filesystem::remove_all(entry.path());
    }
  } catch(const std::exception&) { /* Failed cleanup cannot invalidate a committed save. */ }
}
}

std::filesystem::path GameSaveStore::defaultRoot() {
  const auto environment=[](const char* name) {
    const char* value=std::getenv(name);
    const std::filesystem::path path=value?value:"";
    return path.is_absolute()?path:std::filesystem::path{};
  };
#ifdef _WIN32
  const auto base=environment("LOCALAPPDATA");
  if(!base.empty()) return base/"UrbanSimCore/Saves";
#elif defined(__APPLE__)
  const auto base=environment("HOME");
  if(!base.empty()) return base/"Library/Application Support/UrbanSimCore/Saves";
#else
  const auto data=environment("XDG_DATA_HOME");
  if(!data.empty()) return data/"urban-sim/saves";
  const auto home=environment("HOME");
  if(!home.empty()) return home/".local/share/urban-sim/saves";
#endif
  return {}; // Never silently save beside an installed executable.
}
bool GameSaveStore::validName(const std::string& name) {
  if(name.empty() || name.size()>48 || name.front()==' ' || name.back()==' ' ||
     name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-")!=std::string::npos) return false;
  std::string upper=name;
  for(auto& c:upper) if(c>='a' && c<='z') c=char(c-'a'+'A');
  if(upper=="CON" || upper=="PRN" || upper=="AUX" || upper=="NUL") return false;
  return !(upper.size()==4 && (upper.substr(0,3)=="COM" || upper.substr(0,3)=="LPT") && upper[3]>='1' && upper[3]<='9');
}
std::vector<GameSaveStore::Entry> GameSaveStore::entries() const {
  std::vector<Entry> result;
  if(root_.empty() || !std::filesystem::exists(root_)) return result;
  for(const auto& file:std::filesystem::directory_iterator(root_)) {
    if(!file.is_regular_file() || file.path().extension()!=".json") continue;
    Entry entry;entry.name=file.path().stem().string();
    for(int i=1;i<=3;++i) {
      const auto suffix=".backup"+std::to_string(i);
      if(entry.name.size()>suffix.size() && entry.name.substr(entry.name.size()-suffix.size())==suffix) {
        entry.name.resize(entry.name.size()-suffix.size());entry.backup=i;break;
      }
    }
    if(!validName(entry.name)) continue;
    const auto path=resolve(entry.name,entry.backup);
    if(!path.empty()) {
      const auto document=json::parse(read(path));entry.tick=document.at("tick").get<uint32_t>();
      entry.valid=GameplaySessionSystem::readMapDimensions(path,entry.width,entry.height);
    }
    result.push_back(std::move(entry));
  }
  std::sort(result.begin(),result.end(),[](const Entry& a,const Entry& b) {
    return a.name==b.name?a.backup<b.backup:a.name<b.name;
  });
  return result;
}
std::string GameSaveStore::resolve(const std::string& name,int backup,std::string* reason) const {
  try {
    if(root_.empty() || !validName(name) || backup<0 || backup>3) throw std::runtime_error("Invalid save location or slot name");
    const auto manifest=json::parse(read(slot(root_,name,backup)));
    const auto path=std::filesystem::absolute(root_/"objects"/generation(manifest)/"session.json");
    validate(path);error(reason,"");return path.string();
  } catch(const std::exception& e) {error(reason,e.what());return {};}
}
std::string GameSaveStore::recover(const std::string& name,bool& recovered,std::string* reason) const {
  recovered=false;std::string firstError;
  for(int i=0;i<=3;++i) {
    auto path=resolve(name,i,reason);
    if(!path.empty()) {recovered=i!=0; if(recovered) error(reason,"Recovered "+name+" from backup "+std::to_string(i));return path;}
    if(i==0 && reason) firstError=*reason;
  }
  error(reason,"Cannot load "+name+" or its backups: "+firstError);return {};
}
bool GameSaveStore::save(const std::string& name,const CityMap& map,const RoadNetwork& roads,
    const EntityStore& entities,const PopulationStore& people,const GameplaySessionState& session,std::string* reason) const {
  std::lock_guard<std::mutex> lock(writer);
  try {
    if(root_.empty() || !validName(name)) throw std::runtime_error("Invalid save location or slot name");
    std::filesystem::create_directories(root_/"objects");
    std::filesystem::path directory;std::string id;
    do {
      id="g-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
      directory=root_/"objects"/id;
    } while(!std::filesystem::create_directory(directory));
    const auto path=directory/"session.json";
    std::string why;
    if(!GameplaySessionSystem::save(path.string(),map,roads,entities,people,session,&why)) throw std::runtime_error(why);
    auto document=json::parse(read(path));document["citySnapshot"]="session.json.city.json";
    write(path,document.dump(2));syncFile(directory/"session.json.city.json");syncDirectory(directory);syncDirectory(root_/"objects");
    validate(path);
    // A damaged primary must never displace the last good backup.
    if(!resolve(name).empty()) {
      for(int i=3;i>=1;--i) {
        const auto previous=slot(root_,name,i-1);
        if(!resolve(name,i-1).empty()) publish(slot(root_,name,i),read(previous));
      }
    }
    publish(slot(root_,name,0),json{{"saveStoreVersion",1},{"name",name},{"generation",id},{"tick",session.tick}}.dump(2));
    error(reason,"");collect(root_);return true;
  } catch(const std::exception& e) {error(reason,std::string("Save failed: ")+e.what());return false;}
}
