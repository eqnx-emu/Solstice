/**
 * @file src/steam_library.h
 * @brief Declarations for importing the installed Steam games as applications.
 */
#pragma once

// standard includes
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace steam_library {
  /**
   * @brief Field of an apps.json entry holding the Steam app id it was imported from.
   * @details Only entries carrying it are removed again when the game is uninstalled.
   */
  constexpr std::string_view appid_field = "steam-appid";

  /**
   * @brief Top-level field of apps.json listing the Steam app ids the user deleted, never imported again.
   */
  constexpr std::string_view ignored_field = "steam-ignored";

  /**
   * @brief One node of a Valve KeyValues (VDF/ACF) document.
   */
  struct vdf_node_t {
    std::map<std::string, std::string> values;  ///< String values, keys lowercased.
    std::vector<std::pair<std::string, vdf_node_t>> children;  ///< Child sections in file order, keys lowercased.

    /**
     * @brief Find a value by key, ignoring case.
     *
     * @param key Key to look up.
     * @return The value, or nullptr when missing.
     */
    const std::string *value(std::string_view key) const;

    /**
     * @brief Find the first child section by key, ignoring case.
     *
     * @param key Key to look up.
     * @return The section, or nullptr when missing.
     */
    const vdf_node_t *child(std::string_view key) const;
  };

  /**
   * @brief An installed Steam game.
   */
  struct game_t {
    std::string appid;  ///< Steam app id.
    std::string name;  ///< Name shown in the Steam library.
  };

  /**
   * @brief Outcome of an import.
   */
  struct import_result_t {
    bool steam_found = false;  ///< A Steam installation was found.
    int added = 0;  ///< Games added to the applications.
    int removed = 0;  ///< Imported games removed because they are no longer installed.
    int installed = 0;  ///< Installed games found.
  };

  /**
   * @brief Parse a Valve KeyValues text document (libraryfolders.vdf, appmanifest_*.acf).
   *
   * @param text Document text.
   * @return The root node, or std::nullopt when the document is malformed.
   */
  std::optional<vdf_node_t> parse_vdf(std::string_view text);

  /**
   * @brief Library folders listed in a libraryfolders.vdf document.
   *
   * @param libraryfolders Parsed libraryfolders.vdf.
   * @return The folder paths, UTF-8.
   */
  std::vector<std::string> library_paths(const vdf_node_t &libraryfolders);

  /**
   * @brief Read the game described by an appmanifest_*.acf document.
   *
   * @param manifest Parsed manifest.
   * @return The game, or std::nullopt when it is not a fully installed game (tools and runtimes included).
   */
  std::optional<game_t> game_from_manifest(const vdf_node_t &manifest);

  /**
   * @brief Command that starts a Steam game on this platform.
   *
   * @param appid Steam app id.
   * @return The command for the "detached" list of an application.
   */
  std::string launch_command(std::string_view appid);

  /**
   * @brief Merge the installed games into an apps.json document.
   * @details Games already present, imported or added by hand with the same launch URL, are left untouched.
   * Imported entries whose game is no longer installed are removed, and games the user deleted (see forget_game())
   * are skipped. Other entries are never changed.
   *
   * @param apps_file Parsed apps.json; its "apps" array is updated in place.
   * @param games Installed games.
   * @param cover_for Image path to use for a new game, empty for none.
   * @return Number of games added and removed (steam_found and installed are not set).
   */
  import_result_t merge_games(nlohmann::json &apps_file, const std::vector<game_t> &games, const std::function<std::string(const game_t &)> &cover_for);

  /**
   * @brief Remember that an imported game was deleted, so the next import skips it.
   *
   * @param apps_file Parsed apps.json; its "steam-ignored" list is updated in place.
   * @param app The entry being deleted; entries that were not imported are ignored.
   */
  void forget_game(nlohmann::json &apps_file, const nlohmann::json &app);

  /**
   * @brief Find the installed Steam games and update the applications file with them.
   *
   * @param apps_file_path Path of apps.json.
   * @return What was imported.
   */
  import_result_t import_games(const std::string &apps_file_path);
}  // namespace steam_library
