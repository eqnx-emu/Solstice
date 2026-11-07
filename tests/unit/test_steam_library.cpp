/**
 * @file tests/unit/test_steam_library.cpp
 * @brief Test src/steam_library.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <string>

// local includes
#include <src/steam_library.h>

namespace {
  constexpr auto libraryfolders = R"(
"libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"apps"
		{
			"228980"		"0"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
	}
}
)";

  steam_library::vdf_node_t manifest(const std::string &appid, const std::string &name, const std::string &flags = "4") {
    auto doc = steam_library::parse_vdf(
      "\"AppState\"\n{\n\t\"appid\"\t\t\"" + appid + "\"\n\t\"name\"\t\t\"" + name + "\"\n\t\"StateFlags\"\t\t\"" + flags + "\"\n}\n"
    );
    return doc.value();
  }
}  // namespace

TEST(SteamLibrary, ParsesNestedSectionsAndEscapes) {
  const auto doc = steam_library::parse_vdf(libraryfolders);
  ASSERT_TRUE(doc);
  const auto paths = steam_library::library_paths(*doc);
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "C:\\Program Files (x86)\\Steam");
  EXPECT_EQ(paths[1], "D:\\SteamLibrary");
}

TEST(SteamLibrary, ReadsTheOldLibraryFoldersFormat) {
  const auto doc = steam_library::parse_vdf("\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\"1\"\n\t\"1\"\t\"E:\\\\Games\"\n}\n");
  ASSERT_TRUE(doc);
  const auto paths = steam_library::library_paths(*doc);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "E:\\Games");
}

TEST(SteamLibrary, RejectsMalformedDocuments) {
  EXPECT_FALSE(steam_library::parse_vdf("\"AppState\"\n{\n\t\"appid\" \"10\"\n"));
  EXPECT_FALSE(steam_library::parse_vdf("}"));
  EXPECT_FALSE(steam_library::parse_vdf("\"unterminated"));
}

TEST(SteamLibrary, IgnoresCommentsAndKeyCase) {
  const auto doc = steam_library::parse_vdf("// comment\n\"AppState\" { \"AppID\" \"620\" // trailing\n \"Name\" \"Portal 2\" \"StateFlags\" \"4\" }");
  ASSERT_TRUE(doc);
  const auto game = steam_library::game_from_manifest(*doc);
  ASSERT_TRUE(game);
  EXPECT_EQ(game->appid, "620");
  EXPECT_EQ(game->name, "Portal 2");
}

TEST(SteamLibrary, SkipsGamesNotFullyInstalled) {
  EXPECT_TRUE(steam_library::game_from_manifest(manifest("620", "Portal 2", "4")));
  EXPECT_TRUE(steam_library::game_from_manifest(manifest("620", "Portal 2", "6")));
  EXPECT_FALSE(steam_library::game_from_manifest(manifest("620", "Portal 2", "1026")));
}

TEST(SteamLibrary, SkipsToolsAndRuntimes) {
  EXPECT_FALSE(steam_library::game_from_manifest(manifest("228980", "Steamworks Common Redistributables")));
  EXPECT_FALSE(steam_library::game_from_manifest(manifest("2348590", "Proton 8.0")));
  EXPECT_FALSE(steam_library::game_from_manifest(manifest("1628350", "Steam Linux Runtime 3.0 (sniper)")));
}

TEST(SteamLibrary, MergeAddsNewGamesWithCoverAndMarker) {
  nlohmann::json apps = {{"env", nlohmann::json::object()}, {"apps", nlohmann::json::array({{{"name", "Desktop"}}})}};
  const std::vector<steam_library::game_t> games {{"620", "Portal 2"}, {"400", "Portal"}};

  const auto result = steam_library::merge_games(apps, games, [](const steam_library::game_t &game) {
    return "covers/steam_" + game.appid + ".jpg";
  });

  EXPECT_EQ(result.added, 2);
  EXPECT_EQ(result.removed, 0);
  ASSERT_EQ(apps["apps"].size(), 3u);
  // Sorted by name.
  EXPECT_EQ(apps["apps"][0]["name"], "Desktop");
  EXPECT_EQ(apps["apps"][1]["name"], "Portal");
  const auto &portal2 = apps["apps"][2];
  EXPECT_EQ(portal2["name"], "Portal 2");
  EXPECT_EQ(portal2["steam-appid"], "620");
  EXPECT_EQ(portal2["image-path"], "covers/steam_620.jpg");
  ASSERT_EQ(portal2["detached"].size(), 1u);
  EXPECT_EQ(portal2["detached"][0], steam_library::launch_command("620"));
}

TEST(SteamLibrary, MergeKeepsExistingAndHandMadeEntries) {
  nlohmann::json apps = {{"apps", nlohmann::json::array({
                                    {{"name", "My Portal 2"}, {"steam-appid", "620"}, {"image-path", "custom.png"}},
                                    {{"name", "Hand made"}, {"detached", {"steam://rungameid/400"}}},
                                  })}};
  const std::vector<steam_library::game_t> games {{"620", "Portal 2"}, {"400", "Portal"}};

  const auto result = steam_library::merge_games(apps, games, {});

  EXPECT_EQ(result.added, 0);
  EXPECT_EQ(result.removed, 0);
  ASSERT_EQ(apps["apps"].size(), 2u);
  EXPECT_EQ(apps["apps"][1]["name"], "My Portal 2");
  EXPECT_EQ(apps["apps"][1]["image-path"], "custom.png");
}

TEST(SteamLibrary, MergeRemovesOnlyImportedGamesThatWereUninstalled) {
  nlohmann::json apps = {{"apps", nlohmann::json::array({
                                    {{"name", "Portal 2"}, {"steam-appid", "620"}},
                                    {{"name", "Hand made"}, {"detached", {"steam://rungameid/400"}}},
                                  })}};

  const auto result = steam_library::merge_games(apps, {}, {});

  EXPECT_EQ(result.removed, 1);
  ASSERT_EQ(apps["apps"].size(), 1u);
  EXPECT_EQ(apps["apps"][0]["name"], "Hand made");
}

TEST(SteamLibrary, DeletedGamesAreNotImportedAgain) {
  nlohmann::json apps = {{"apps", nlohmann::json::array()}};
  steam_library::forget_game(apps, {{"name", "Portal 2"}, {"steam-appid", "620"}});
  steam_library::forget_game(apps, {{"name", "Portal 2"}, {"steam-appid", "620"}});
  steam_library::forget_game(apps, {{"name", "Hand made"}});
  ASSERT_EQ(apps["steam-ignored"].size(), 1u);

  const auto result = steam_library::merge_games(apps, {{"620", "Portal 2"}, {"400", "Portal"}}, {});

  EXPECT_EQ(result.added, 1);
  ASSERT_EQ(apps["apps"].size(), 1u);
  EXPECT_EQ(apps["apps"][0]["name"], "Portal");
}
