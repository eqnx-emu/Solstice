/**
 * @file tests/unit/platform/windows/test_nvprefs_undo_data.cpp
 * @brief Regression coverage for absent and zero NVIDIA preference restore values.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32

  // standard includes
  #include <type_traits>

  // local includes
  #include "src/platform/windows/nvprefs/undo_data.h"
  #include "src/platform/windows/nvprefs/undo_file.h"

static_assert(std::is_same_v<decltype(&nvprefs::undo_file_t::open_existing_file), std::optional<nvprefs::undo_file_t> (*)(const std::filesystem::path &, bool &)>);
static_assert(std::is_same_v<decltype(&nvprefs::undo_file_t::create_new_file), std::optional<nvprefs::undo_file_t> (*)(const std::filesystem::path &)>);

/**
 * @brief Input-only paths preserve creation, sharing-error, and missing-file behavior.
 */
TEST_F(BaseTest, NvPrefsUndoFilesAcceptConstPathsAndReportOpenFailures) {
  const auto path = std::filesystem::path {SUNSHINE_TEST_BIN_DIR} / "nvprefs_const_path_test.json";
  std::filesystem::remove(path);
  auto cleanup = util::fail_guard([&path] {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  });

  {
    const auto created = nvprefs::undo_file_t::create_new_file(path);
    ASSERT_TRUE(created.has_value());
    bool access_denied = false;
    EXPECT_FALSE(nvprefs::undo_file_t::open_existing_file(path, access_denied).has_value());
    EXPECT_TRUE(access_denied);
  }

  bool access_denied = true;
  auto reopened = nvprefs::undo_file_t::open_existing_file(path, access_denied);
  ASSERT_TRUE(reopened.has_value());
  EXPECT_FALSE(access_denied);
  reopened.reset();
  EXPECT_FALSE(nvprefs::undo_file_t::create_new_file(path).has_value());

  ASSERT_TRUE(std::filesystem::remove(path));
  EXPECT_FALSE(nvprefs::undo_file_t::open_existing_file(path, access_denied).has_value());
  EXPECT_FALSE(access_denied);
}

/**
 * @brief Serialization preserves the distinction between zero and an absent driver setting.
 */
TEST(NvPrefsUndoDataTest, PreservesZeroAndAbsentRestoreValues) {
  for (const auto value : {std::optional<std::uint32_t> {}, std::optional<std::uint32_t> {0}, std::optional<std::uint32_t> {7}}) {
    nvprefs::undo_data_t original;
    original.set_opengl_swapchain(1, value);
    const auto serialized = original.write();
    nvprefs::undo_data_t restored;
    restored.read(std::vector<char> {serialized.begin(), serialized.end()});
    const auto setting = restored.get_opengl_swapchain();
    ASSERT_TRUE(setting.has_value());
    EXPECT_EQ(setting->our_value, 1);
    EXPECT_EQ(setting->undo_value, value);
  }
}

/**
 * @brief The frame rate limiter record survives serialization, including an absent restore value.
 */
TEST(NvPrefsUndoDataTest, FrameLimiterRoundTripsThroughSerialization) {
  for (const auto value : {std::optional<std::uint32_t> {}, std::optional<std::uint32_t> {0}, std::optional<std::uint32_t> {144}}) {
    nvprefs::undo_data_t original;
    original.set_frame_limiter(60, value);
    const auto serialized = original.write();
    nvprefs::undo_data_t restored;
    restored.read(std::vector<char> {serialized.begin(), serialized.end()});
    const auto limiter = restored.get_frame_limiter();
    ASSERT_TRUE(limiter.has_value());
    EXPECT_EQ(limiter->our_value, 60);
    EXPECT_EQ(limiter->undo_value, value);
    EXPECT_FALSE(restored.get_opengl_swapchain().has_value());
  }
}

/**
 * @brief Undo files written before the limiter existed still load.
 */
TEST(NvPrefsUndoDataTest, ReadsUndoFilesWithoutFrameLimiter) {
  const std::string legacy = R"({"opengl_swapchain":{"our_value":1,"undo_value":null}})";
  nvprefs::undo_data_t restored;
  restored.read(std::vector<char> {legacy.begin(), legacy.end()});
  ASSERT_TRUE(restored.get_opengl_swapchain().has_value());
  EXPECT_FALSE(restored.get_frame_limiter().has_value());
  EXPECT_FALSE(restored.empty());
}

/**
 * @brief Clearing the limiter leaves other records, and empty() reports when nothing is left.
 */
TEST(NvPrefsUndoDataTest, ClearFrameLimiterAndEmpty) {
  nvprefs::undo_data_t data;
  EXPECT_TRUE(data.empty());

  data.set_frame_limiter(60, std::nullopt);
  EXPECT_FALSE(data.empty());
  data.clear_frame_limiter();
  EXPECT_FALSE(data.get_frame_limiter().has_value());
  EXPECT_TRUE(data.empty());

  data.set_opengl_swapchain(1, 0);
  data.set_frame_limiter(60, 30);
  data.clear_frame_limiter();
  EXPECT_FALSE(data.empty());
  EXPECT_TRUE(data.get_opengl_swapchain().has_value());
}

/**
 * @brief Merging keeps the first recorded limiter restore value (the user's own setting).
 */
TEST(NvPrefsUndoDataTest, MergeKeepsOriginalFrameLimiterUndoValue) {
  nvprefs::undo_data_t current;
  current.set_frame_limiter(60, 144);

  nvprefs::undo_data_t newer;
  newer.set_frame_limiter(120, 60);
  newer.set_opengl_swapchain(1, 0);
  current.merge(newer);

  const auto limiter = current.get_frame_limiter();
  ASSERT_TRUE(limiter.has_value());
  EXPECT_EQ(limiter->our_value, 60);
  EXPECT_EQ(limiter->undo_value, 144);
  EXPECT_TRUE(current.get_opengl_swapchain().has_value());

  nvprefs::undo_data_t empty;
  empty.merge(newer);
  ASSERT_TRUE(empty.get_frame_limiter().has_value());
  EXPECT_EQ(empty.get_frame_limiter()->undo_value, 60);
}

#endif
