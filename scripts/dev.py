#!/usr/bin/env python3
"""Bootstrap local ArcadeEngine dependencies and configure a CMake preset."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

from ci import setup_msvc_environment


ROOT = Path(__file__).resolve().parent.parent


def run(command: list[str], *, cwd: Path = ROOT) -> None:
    print("\n$", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def windows_install() -> None:
    llvm_bin = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin"
    candidates = [
        llvm_bin,
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "CMake" / "bin",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Ninja",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Links",
    ]
    installed_bins = [str(path) for path in candidates if path.is_dir()]
    if installed_bins:
        os.environ["PATH"] = os.pathsep.join(installed_bins + [os.environ.get("PATH", "")])

    package_tools = {
        "cmake": "Kitware.CMake",
        "ninja": "Ninja-build.Ninja",
        "clang-format": "LLVM.LLVM",
        "clang-tidy": "LLVM.LLVM",
    }
    missing_packages = list(dict.fromkeys(
        package_id
        for tool, package_id in package_tools.items()
        if shutil.which(tool) is None
    ))

    if missing_packages:
        winget = shutil.which("winget")
        if winget is None:
            raise RuntimeError(
                "winget is required to install missing Windows tools: "
                + ", ".join(missing_packages)
                + ". Install App Installer or install those tools manually."
            )

        for package_id in missing_packages:
            try:
                run([
                    winget, "install", "--id", package_id, "--exact", "--no-upgrade",
                    "--accept-source-agreements", "--accept-package-agreements",
                ])
            except subprocess.CalledProcessError as error:
                # WinGet returns this HRESULT when its package database knows
                # the package is installed already. Continue and verify that
                # the executable can be found on PATH below.
                if error.returncode & 0xFFFFFFFF != 0x8A150061:
                    raise
                print(f"{package_id} is already installed; checking its tools on PATH.")

        if installed_bins:
            os.environ["PATH"] = os.pathsep.join(installed_bins + [os.environ.get("PATH", "")])

    still_missing = [tool for tool in package_tools if shutil.which(tool) is None]
    if still_missing:
        raise RuntimeError(
            "These tools are still missing from PATH after setup: "
            + ", ".join(still_missing)
            + ". Restart the terminal or add their install folders to PATH."
        )


def linux_install() -> None:
    if shutil.which("apt-get") is None:
        raise RuntimeError("Automatic Linux dependency installation currently supports apt-based distributions.")
    sudo = shutil.which("sudo")
    if sudo is None and hasattr(os, "geteuid") and os.geteuid() != 0:
        raise RuntimeError("Install dependencies as root or install sudo, then rerun setup.")
    prefix = [] if hasattr(os, "geteuid") and os.geteuid() == 0 else [sudo or "sudo"]
    run(prefix + ["apt-get", "update"])
    run(prefix + [
        "apt-get", "install", "--yes", "cmake", "ninja-build", "git", "python3",
        "clang-format", "clang-tidy", "libvulkan-dev", "libwayland-dev",
        "wayland-protocols", "libxkbcommon-dev", "xorg-dev",
    ])


def macos_install() -> None:
    brew = shutil.which("brew")
    if brew is None:
        raise RuntimeError("Install Homebrew first: https://brew.sh")
    run([brew, "install", "cmake", "ninja", "llvm", "vulkan-headers", "vulkan-loader", "molten-vk"])

    prefix = subprocess.run(
        [brew, "--prefix", "llvm"], check=True, capture_output=True, text=True
    ).stdout.strip()
    llvm_bin = Path(prefix) / "bin"
    os.environ["PATH"] = str(llvm_bin) + os.pathsep + os.environ.get("PATH", "")


def install_dependencies() -> None:
    system = platform.system()
    if system == "Windows":
        windows_install()
    elif system == "Linux":
        linux_install()
    elif system == "Darwin":
        macos_install()
    else:
        raise RuntimeError(f"Automatic dependency setup is not supported on {system}.")


def prepare_windows_git_environment(git: str) -> None:
    if platform.system() != "Windows":
        return

    executable = Path(git).resolve()
    git_root = executable.parent.parent
    support_directories = (git_root / "usr" / "bin", git_root / "bin")
    current_path = os.environ.get("PATH", "").split(os.pathsep)
    additions = [
        str(path)
        for path in support_directories
        if path.is_dir() and str(path) not in current_path
    ]
    if additions:
        os.environ["PATH"] = os.pathsep.join(additions + current_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("debug", "release"), default="debug")
    parser.add_argument(
        "--skip-install", action="store_true",
        help="Skip OS package installation; still initialize submodules and configure CMake.",
    )
    parser.add_argument(
        "--no-build", action="store_true",
        help="Configure the selected CMake preset without building it.",
    )
    args = parser.parse_args()

    try:
        if not args.skip_install:
            install_dependencies()

        git = shutil.which("git")
        if git is None:
            raise RuntimeError("Git is required to initialize the pinned submodules.")
        prepare_windows_git_environment(git)
        run([git, "submodule", "update", "--init", "--recursive"])

        msvc_environment = setup_msvc_environment()
        if msvc_environment is not None:
            os.environ.update(msvc_environment)

        cmake = shutil.which("cmake")
        if cmake is None:
            raise RuntimeError("CMake was not found after dependency setup. Restart the terminal and rerun setup.")
        run([cmake, "--preset", args.preset])
        if not args.no_build:
            run([cmake, "--build", "--preset", args.preset])
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"\nsetup failed: {error}", file=sys.stderr)
        return getattr(error, "returncode", 1) or 1

    print("\nArcadeEngine dependencies and build are ready.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
