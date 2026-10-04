#pragma once

#include <memory>

#include "Core/Export.hpp"

class Window;

// Owns the native graphics device and its NVRHI wrapper. Presentation remains
// a platform-window responsibility.
class GraphicsDevice
{
public:
    ARCADE_ENGINE_API explicit GraphicsDevice(const Window& window);
    ARCADE_ENGINE_API ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;
    GraphicsDevice(GraphicsDevice&&) = delete;
    GraphicsDevice& operator=(GraphicsDevice&&) = delete;

    [[nodiscard]] ARCADE_ENGINE_API bool WaitForIdle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
