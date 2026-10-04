# Development scripts

Run the platform wrapper from any working directory:

| Task | Windows | Linux / macOS |
| --- | --- | --- |
| Install prerequisites, fetch submodules, configure and build Debug | `scripts\\setup.bat` | `sh scripts/setup.sh` |
| Configure/build without installing OS packages | `scripts\\setup.bat --skip-install` | `sh scripts/setup.sh --skip-install` |
| Apply clang-format | `scripts\\format.bat` | `sh scripts/format.sh` |
| Run clang-tidy | `scripts\\lint.bat` | `sh scripts/lint.sh` |
| Build and run headless tests | `scripts\\test.bat [debug|release]` | `sh scripts/test.sh [debug|release]` |

The setup script uses WinGet on Windows, apt on Linux, and Homebrew on macOS. It
initializes the repository's pinned recursive submodules, configures a CMake
preset, and builds it. Use `--preset release` to build Release, or `--no-build`
to configure without compiling. `--skip-install` leaves OS package management
to you but still fetches submodules and configures CMake.

Windows setup requires Git, the Python launcher, and Visual Studio C++ Build
Tools to be installed first. macOS setup requires Xcode Command Line Tools and
Homebrew. Linux setup targets apt-based distributions and requires Git plus
Python before the script starts. Setup installs the remaining CMake, Ninja,
format/lint, and Vulkan development packages.
