/**
 * @file src/platform/windows/nvprefs/nvprefs_interface.h
 * @brief Declarations for nvidia preferences interface.
 */
#pragma once

// standard includes
#include <cstdint>
#include <memory>

namespace nvprefs {

  /**
   * @brief High-level NVIDIA profile preferences interface with undo support.
   */
  class nvprefs_interface {
  public:
    nvprefs_interface();
    ~nvprefs_interface();

    /**
     * @brief Load persisted state from its backing store.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool load();

    /**
     * @brief Release loaded NVIDIA preference state.
     */
    void unload();

    /**
     * @brief Restore NVIDIA settings from the undo file, then remove it.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool restore_from_and_delete_undo_file_if_exists();

    /**
     * @brief Apply Sunshine-specific NVIDIA application profile changes.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool modify_application_profile();

    /**
     * @brief Apply Sunshine-specific NVIDIA global profile changes.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool modify_global_profile();

    /**
     * @brief Check whether this interface owns the active undo file.
     *
     * @return True when this interface owns the undo file; querying does not load NVIDIA APIs.
     */
    bool owning_undo_file() const;

    /**
     * @brief Restore NVIDIA global profile settings from saved undo data.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool restore_global_profile();

    /**
     * @brief Cap every game to the given frame rate through the driver's global frame rate limiter.
     * @details The previous limiter value is recorded in the undo file so it is restored even if
     * Sunshine terminates improperly.
     *
     * @param fps Frame rate limit to apply.
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool apply_frame_limiter(uint32_t fps);

    /**
     * @brief Restore the global frame rate limiter changed by apply_frame_limiter().
     *
     * @return True when the NVIDIA preferences operation succeeds or there was nothing to restore.
     */
    bool restore_frame_limiter();

  private:
    /**
     * @brief Create the undo file if needed and write the current undo data to it.
     *
     * @return True when the undo file holds the current undo data.
     */
    bool write_undo_file();

    struct impl;
    std::unique_ptr<impl> pimpl;
  };

}  // namespace nvprefs
