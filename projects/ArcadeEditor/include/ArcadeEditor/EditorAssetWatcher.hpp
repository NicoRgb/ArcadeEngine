#pragma once

#include "Core/Result.hpp"

#include <filesystem>
#include <memory>
#include <vector>

class EditorAssetWatcher
{
public:
    EditorAssetWatcher();
    ~EditorAssetWatcher();
    EditorAssetWatcher(const EditorAssetWatcher&) = delete;
    EditorAssetWatcher& operator=(const EditorAssetWatcher&) = delete;

    [[nodiscard]] Result<void> Start(const std::filesystem::path& root);
    void Stop() noexcept;
    [[nodiscard]] std::vector<std::filesystem::path> DrainChanges();
    [[nodiscard]] bool IsWatching() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
