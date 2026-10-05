#pragma once

#include "ArcadeEditor/EditorPanel.hpp"

#include <memory>

[[nodiscard]] std::unique_ptr<EditorPanel> CreateAssetBrowserPanel();
