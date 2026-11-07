/**
 * @file src/steam_library.cpp
 * @brief Definitions for importing the installed Steam games as applications.
 */
// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <sstream>

// local includes
#include "file_handler.h"
#include "httpcommon.h"
#include "logging.h"
#include "platform/common.h"
#include "process.h"
#include "steam_library.h"

// platform includes (after the local ones: boost.asio must see winsock2.h first)
#ifdef _WIN32
  #include <windows.h>
#endif

using namespace std::literals;
namespace fs = std::filesystem;

namespace steam_library {
  namespace {
    std::string lower(std::string_view text) {
      std::string out(text);
      std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    fs::path utf8_path(const std::string &text) {
      return fs::path(std::u8string(text.begin(), text.end()));
    }

    std::string path_utf8(const fs::path &path) {
      const auto text = path.u8string();
      return std::string(text.begin(), text.end());
    }

    /**
     * @brief Tokenizer over KeyValues text: quoted or bare strings, braces, // comments.
     */
    class vdf_reader_t {
    public:
      explicit vdf_reader_t(std::string_view text):
          text {text} {
      }

      enum class token_e {
        string,
        open,
        close,
        end,
        error
      };

      token_e next(std::string &out) {
        skip_space();
        if (pos >= text.size()) {
          return token_e::end;
        }
        const char c = text[pos];
        if (c == '{') {
          pos++;
          return token_e::open;
        }
        if (c == '}') {
          pos++;
          return token_e::close;
        }
        out.clear();
        if (c == '"') {
          pos++;
          while (pos < text.size() && text[pos] != '"') {
            if (text[pos] == '\\' && pos + 1 < text.size()) {
              const char e = text[++pos];
              out += e == 'n' ? '\n' : e == 't' ? '\t' :
                                                  e;
            } else {
              out += text[pos];
            }
            pos++;
          }
          if (pos >= text.size()) {
            return token_e::error;
          }
          pos++;
          return token_e::string;
        }
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) && text[pos] != '{' && text[pos] != '}' && text[pos] != '"') {
          out += text[pos++];
        }
        return token_e::string;
      }

    private:
      void skip_space() {
        while (pos < text.size()) {
          if (std::isspace(static_cast<unsigned char>(text[pos]))) {
            pos++;
          } else if (text.compare(pos, 2, "//") == 0) {
            while (pos < text.size() && text[pos] != '\n') {
              pos++;
            }
          } else {
            break;
          }
        }
      }

      std::string_view text;
      std::size_t pos = 0;
    };

    bool parse_section(vdf_reader_t &reader, vdf_node_t &node, bool top_level) {
      std::string key;
      std::string value;
      while (true) {
        switch (reader.next(key)) {
          case vdf_reader_t::token_e::end:
            return top_level;
          case vdf_reader_t::token_e::close:
            return !top_level;
          case vdf_reader_t::token_e::string:
            break;
          default:
            return false;
        }
        switch (reader.next(value)) {
          case vdf_reader_t::token_e::string:
            node.values[lower(key)] = value;
            break;
          case vdf_reader_t::token_e::open:
            {
              vdf_node_t child;
              if (!parse_section(reader, child, false)) {
                return false;
              }
              node.children.emplace_back(lower(key), std::move(child));
              break;
            }
          default:
            return false;
        }
      }
    }

    std::optional<std::string> read_text(const fs::path &path) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return std::nullopt;
      }
      std::stringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    std::optional<vdf_node_t> parse_file(const fs::path &path) {
      const auto text = read_text(path);
      return text ? parse_vdf(*text) : std::nullopt;
    }

#ifdef _WIN32
    std::optional<fs::path> registry_path(HKEY root, const wchar_t *key, const wchar_t *value) {
      std::array<wchar_t, MAX_PATH> buffer {};
      DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
      if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
      }
      return fs::path(buffer.data());
    }
#endif

    /**
     * @brief Where Steam is installed.
     * @details Solstice usually runs as a service, so the machine-wide registry key is read rather than the user's.
     */
    std::optional<fs::path> find_steam_root() {
      std::vector<fs::path> candidates;
#ifdef _WIN32
      for (auto *key : {L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"SOFTWARE\\Valve\\Steam"}) {
        if (auto path = registry_path(HKEY_LOCAL_MACHINE, key, L"InstallPath")) {
          candidates.push_back(*path);
        }
      }
      if (auto path = registry_path(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", L"SteamPath")) {
        candidates.push_back(*path);
      }
      candidates.emplace_back(L"C:\\Program Files (x86)\\Steam");
      candidates.emplace_back(L"C:\\Program Files\\Steam");
#else
      if (const char *home = std::getenv("HOME")) {
        const fs::path h {home};
  #ifdef __APPLE__
        candidates.push_back(h / "Library/Application Support/Steam");
  #else
        candidates.push_back(h / ".steam/steam");
        candidates.push_back(h / ".local/share/Steam");
        candidates.push_back(h / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
  #endif
      }
#endif
      for (const auto &path : candidates) {
        std::error_code ec;
        if (fs::exists(path / "steamapps", ec)) {
          return path;
        }
      }
      return std::nullopt;
    }

    std::vector<game_t> installed_games(const fs::path &root) {
      std::vector<fs::path> folders {root};
      if (auto doc = parse_file(root / "steamapps" / "libraryfolders.vdf")) {
        for (const auto &path : library_paths(*doc)) {
          folders.push_back(utf8_path(path));
        }
      }

      std::vector<game_t> games;
      std::set<std::string> seen;
      std::set<fs::path> visited;
      for (const auto &folder : folders) {
        std::error_code ec;
        const auto steamapps = fs::weakly_canonical(folder / "steamapps", ec);
        if (ec || !visited.insert(steamapps).second) {
          continue;
        }
        for (const auto &entry : fs::directory_iterator(steamapps, ec)) {
          const auto name = entry.path().filename().string();
          if (!name.starts_with("appmanifest_") || entry.path().extension() != ".acf") {
            continue;
          }
          auto doc = parse_file(entry.path());
          if (!doc) {
            BOOST_LOG(warning) << "Steam: couldn't read "sv << entry.path().string();
            continue;
          }
          if (auto game = game_from_manifest(*doc); game && seen.insert(game->appid).second) {
            games.push_back(std::move(*game));
          }
        }
      }
      return games;
    }

    bool is_jpeg(const fs::path &path) {
      std::ifstream in(path, std::ios::binary);
      std::array<unsigned char, 3> head {};
      in.read(reinterpret_cast<char *>(head.data()), head.size());
      return in.gcount() == 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF;
    }

    /**
     * @brief The portrait cover Steam cached for a game, if any.
     * @details Current clients keep it as librarycache/<appid>/<hash>/library_capsule.jpg (or library_600x900.jpg),
     * older ones as librarycache/<appid>_library_600x900.jpg.
     */
    std::optional<fs::path> cached_cover(const fs::path &root, const std::string &appid) {
      const auto cache = root / "appcache" / "librarycache";
      std::error_code ec;
      if (const auto old = cache / (appid + "_library_600x900.jpg"); fs::exists(old, ec)) {
        return old;
      }
      const auto dir = cache / appid;
      if (!fs::is_directory(dir, ec)) {
        return std::nullopt;
      }
      std::optional<fs::path> capsule;
      for (const auto &entry : fs::recursive_directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (entry.path().extension() != ".jpg") {
          continue;
        }
        if (name.starts_with("library_600x900")) {
          return entry.path();
        }
        if (name == "library_capsule.jpg") {
          capsule = entry.path();
        }
      }
      return capsule;
    }

    /**
     * @brief Copy (or download) a game's cover into the covers folder.
     *
     * @return The image path for apps.json, empty when no cover was found.
     */
    std::string store_cover(const fs::path &root, const game_t &game) {
      const fs::path dir = platf::appdata() / "covers";
      const fs::path target = dir / ("steam_" + game.appid + ".jpg");
      std::error_code ec;
      fs::create_directories(dir, ec);
      if (fs::exists(target, ec) && is_jpeg(target)) {
        return path_utf8(target);
      }

      if (const auto cached = cached_cover(root, game.appid)) {
        fs::copy_file(*cached, target, fs::copy_options::overwrite_existing, ec);
        if (!ec && is_jpeg(target)) {
          return path_utf8(target);
        }
      }

      const auto url = "https://shared.steamstatic.com/store_item_assets/steam/apps/"s + game.appid + "/library_600x900.jpg";
      if (http::download_file(url, target.string()) && is_jpeg(target)) {
        return path_utf8(target);
      }
      fs::remove(target, ec);
      return {};
    }

    std::string appid_of(const nlohmann::json &app) {
      if (!app.contains(appid_field)) {
        return {};
      }
      const auto &id = app[appid_field];
      return id.is_string() ? id.get<std::string>() : id.is_number_integer() ? std::to_string(id.get<long long>()) :
                                                                                  std::string {};
    }

    /**
     * @brief Steam app id launched by an entry's command or detached commands, if any.
     */
    std::set<std::string> launched_appids(const nlohmann::json &app) {
      std::set<std::string> ids;
      auto scan = [&ids](const nlohmann::json &cmd) {
        if (!cmd.is_string()) {
          return;
        }
        const auto text = cmd.get<std::string>();
        constexpr auto marker = "steam://rungameid/"sv;
        if (const auto at = text.find(marker); at != std::string::npos) {
          auto begin = at + marker.size();
          auto end = begin;
          while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            end++;
          }
          if (end > begin) {
            ids.insert(text.substr(begin, end - begin));
          }
        }
      };
      if (app.contains("cmd")) {
        scan(app["cmd"]);
      }
      if (app.contains("detached") && app["detached"].is_array()) {
        for (const auto &cmd : app["detached"]) {
          scan(cmd);
        }
      }
      return ids;
    }
  }  // namespace

  const std::string *vdf_node_t::value(std::string_view key) const {
    const auto it = values.find(lower(key));
    return it == values.end() ? nullptr : &it->second;
  }

  const vdf_node_t *vdf_node_t::child(std::string_view key) const {
    const auto wanted = lower(key);
    for (const auto &[name, node] : children) {
      if (name == wanted) {
        return &node;
      }
    }
    return nullptr;
  }

  std::optional<vdf_node_t> parse_vdf(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) {
      text.remove_prefix(3);
    }
    vdf_reader_t reader {text};
    vdf_node_t root;
    if (!parse_section(reader, root, true)) {
      return std::nullopt;
    }
    return root;
  }

  std::vector<std::string> library_paths(const vdf_node_t &libraryfolders) {
    std::vector<std::string> paths;
    const auto *folders = libraryfolders.child("libraryfolders");
    if (!folders) {
      return paths;
    }
    for (const auto &[_, folder] : folders->children) {
      if (const auto *path = folder.value("path")) {
        paths.push_back(*path);
      }
    }
    // Old format: "1" "D:\\Games\\Steam" directly under the root section.
    for (const auto &[key, path] : folders->values) {
      if (!key.empty() && std::ranges::all_of(key, [](unsigned char c) {
            return std::isdigit(c);
          })) {
        paths.push_back(path);
      }
    }
    return paths;
  }

  std::optional<game_t> game_from_manifest(const vdf_node_t &manifest) {
    const auto *state = manifest.child("AppState");
    if (!state) {
      return std::nullopt;
    }
    const auto *appid = state->value("appid");
    const auto *name = state->value("name");
    if (!appid || appid->empty() || !name || name->empty()) {
      return std::nullopt;
    }
    if (!std::ranges::all_of(*appid, [](unsigned char c) {
          return std::isdigit(c);
        })) {
      return std::nullopt;
    }

    // StateFlags bit 4: fully installed.
    if (const auto *flags = state->value("StateFlags")) {
      try {
        if ((std::stoul(*flags) & 4) == 0) {
          return std::nullopt;
        }
      } catch (...) {
        return std::nullopt;
      }
    }

    // Redistributables, Proton and the runtimes are installed like games but are not playable.
    static const std::set<std::string, std::less<>> tool_ids {"228980", "1070560", "1391110", "1628350", "1493710", "2180100", "2805730"};
    if (tool_ids.contains(*appid)) {
      return std::nullopt;
    }
    const auto lname = lower(*name);
    for (const auto prefix : {"proton "sv, "proton-"sv, "steam linux runtime"sv, "steamworks common"sv, "steamvr"sv}) {
      if (lname.starts_with(prefix)) {
        return std::nullopt;
      }
    }
    return game_t {*appid, *name};
  }

  std::string launch_command(std::string_view appid) {
#ifdef _WIN32
    return "steam://rungameid/"s + std::string(appid);
#elif defined(__APPLE__)
    return "open steam://rungameid/"s + std::string(appid);
#else
    return "setsid steam steam://rungameid/"s + std::string(appid);
#endif
  }

  import_result_t merge_games(nlohmann::json &apps_file, const std::vector<game_t> &games, const std::function<std::string(const game_t &)> &cover_for) {
    import_result_t result;
    if (!apps_file.contains("apps") || !apps_file["apps"].is_array()) {
      apps_file["apps"] = nlohmann::json::array();
    }
    auto &apps = apps_file["apps"];

    std::set<std::string> ignored;
    if (apps_file.contains(ignored_field) && apps_file[ignored_field].is_array()) {
      for (const auto &id : apps_file[ignored_field]) {
        if (id.is_string()) {
          ignored.insert(id.get<std::string>());
        }
      }
    }

    std::set<std::string> installed;
    for (const auto &game : games) {
      installed.insert(game.appid);
    }

    nlohmann::json kept = nlohmann::json::array();
    std::set<std::string> present;
    for (auto &app : apps) {
      const auto id = appid_of(app);
      if (!id.empty() && !installed.contains(id)) {
        result.removed++;
        continue;
      }
      if (!id.empty()) {
        present.insert(id);
      }
      for (const auto &launched : launched_appids(app)) {
        present.insert(launched);
      }
      kept.push_back(std::move(app));
    }

    for (const auto &game : games) {
      if (present.contains(game.appid) || ignored.contains(game.appid)) {
        continue;
      }
      nlohmann::json app;
      app["name"] = game.name;
      app["detached"] = nlohmann::json::array({launch_command(game.appid)});
      app["image-path"] = cover_for ? cover_for(game) : std::string {};
      app[appid_field] = game.appid;
      kept.push_back(std::move(app));
      present.insert(game.appid);
      result.added++;
    }

    std::stable_sort(kept.begin(), kept.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
      return a.value("name", ""s) < b.value("name", ""s);
    });
    apps = std::move(kept);
    return result;
  }

  void forget_game(nlohmann::json &apps_file, const nlohmann::json &app) {
    const auto id = appid_of(app);
    if (id.empty()) {
      return;
    }
    auto &ignored = apps_file[ignored_field];
    if (!ignored.is_array()) {
      ignored = nlohmann::json::array();
    }
    if (std::ranges::find(ignored, nlohmann::json(id)) == ignored.end()) {
      ignored.push_back(id);
    }
  }

  import_result_t import_games(const std::string &apps_file_path) {
    static std::mutex import_mutex;
    std::lock_guard lock {import_mutex};

    import_result_t result;
    const auto root = find_steam_root();
    if (!root) {
      BOOST_LOG(info) << "Steam: no installation found"sv;
      return result;
    }
    result.steam_found = true;

    const auto games = installed_games(*root);
    result.installed = static_cast<int>(games.size());

    nlohmann::json file_tree = nlohmann::json::parse(file_handler::read_file(apps_file_path.c_str()));
    const auto merged = merge_games(file_tree, games, [&root](const game_t &game) {
      return store_cover(*root, game);
    });
    result.added = merged.added;
    result.removed = merged.removed;

    if (result.added > 0 || result.removed > 0) {
      file_handler::write_file(apps_file_path.c_str(), file_tree.dump(4));
      proc::refresh(apps_file_path);
    }
    BOOST_LOG(info) << "Steam: "sv << result.installed << " games installed, "sv << result.added << " added, "sv << result.removed << " removed"sv;
    return result;
  }
}  // namespace steam_library
