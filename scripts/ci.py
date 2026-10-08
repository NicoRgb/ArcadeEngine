#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

SOURCE_DIRS = (
    ROOT / "projects",
)

SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
}

PRESET_BUILD_DIRS = {
    "debug": ROOT / "build" / "debug",
    "release": ROOT / "build" / "release",
    "clang-tidy": ROOT / "build" / "clang-tidy",
    "asan": ROOT / "build" / "asan",
    "ubsan": ROOT / "build" / "ubsan",
}


def die(message: str, code: int = 1) -> int:
    print(f"error: {message}", file=sys.stderr)
    return code


def find_program(name: str) -> str | None:
    return shutil.which(name)


def require_program(name: str) -> str:
    path = find_program(name)

    if path is None:
        raise RuntimeError(f"'{name}' was not found in PATH")

    return path


def run(
    command: list[str],
    *,
    env: dict[str, str] | None = None,
) -> None:
    print()
    print("$", " ".join(command))

    subprocess.run(
        command,
        cwd=ROOT,
        env=env,
        check=True,
    )


def source_files() -> list[Path]:
    files: list[Path] = []

    for source_dir in SOURCE_DIRS:
        if not source_dir.is_dir():
            continue

        for path in source_dir.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_EXTENSIONS:
                files.append(path)

    return sorted(files)


def format_files(check: bool) -> None:
    clang_format = require_program("clang-format")
    files = source_files()

    if not files:
        print("No C/C++ files found under projects/.")
        return

    # Keep command lines reasonably sized, especially on Windows.
    chunk_size = 100

    for offset in range(0, len(files), chunk_size):
        chunk = files[offset: offset + chunk_size]

        command = [clang_format]

        if check:
            command += [
                "--dry-run",
                "--Werror",
            ]

        command += [str(path) for path in chunk]

        run(command)

    if check:
        print()
        print("Formatting check passed.")
    else:
        print()
        print("Formatting complete.")


def configure(preset: str) -> None:
    cmake = require_program("cmake")

    command = [
        cmake,
        "--preset",
        preset,
    ]

    run(command)


def build(preset: str) -> None:
    cmake = require_program("cmake")

    configure(preset)

    command = [
        cmake,
        "--build",
        "--preset",
        preset,
    ]

    run(command)


def build_only(preset: str) -> None:
    """Build an already-configured preset without checking or configuring it."""
    cmake = require_program("cmake")
    run([cmake, "--build", "--preset", preset])


def lint() -> None:
    require_program("clang-tidy")

    # Project targets opt into CXX_CLANG_TIDY. Vendored ImGui and dependency
    # submodules are intentionally excluded.
    build("clang-tidy")

    print()
    print("clang-tidy passed.")


def sanitizer(preset: str) -> None:
    test(preset)


def test(preset: str) -> None:
    require_program("cmake")

    if preset not in PRESET_BUILD_DIRS:
        raise RuntimeError(
            f"Don't know the build directory for preset '{preset}'."
        )

    build(preset)

    build_dir = PRESET_BUILD_DIRS[preset]
    ctest_file = build_dir / "CTestTestfile.cmake"

    if not ctest_file.exists():
        print()
        print(
            "No CTest configuration found; tests are not configured yet."
        )
        return

    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--build-config",
        preset_to_configuration(preset),
        "--output-on-failure",
    ]

    run(command)

    print()
    print("Tests passed.")


def preset_to_configuration(preset: str) -> str:
    if preset == "release":
        return "Release"

    return "Debug"


def check_submodules() -> None:
    required_paths = (
        ROOT / "extern" / "glfw" / "CMakeLists.txt",
        ROOT / "extern" / "glm" / "CMakeLists.txt",
        ROOT / "extern" / "json" / "single_include" / "nlohmann" / "json.hpp",
        ROOT / "extern" / "nvrhi" / "CMakeLists.txt",
    )

    missing = [
        path
        for path in required_paths
        if not path.exists()
    ]

    if missing:
        message = (
            "One or more git submodules are missing:\n"
            + "\n".join(f"  {path}" for path in missing)
            + "\n\n"
            "Initialize them with:\n"
            "  git submodule update --init --recursive"
        )

        raise RuntimeError(message)


def setup_msvc_environment() -> dict[str, str] | None:
    """
    Populate the Visual Studio developer environment when using Ninja on
    Windows and cl.exe is not already available.

    This makes the Ninja Multi-Config CMake presets usable from a normal
    PowerShell/cmd prompt and on GitHub-hosted Windows runners.
    """
    if platform.system() != "Windows":
        return None

    if find_program("cl"):
        return None

    candidates = [
        Path(os.environ.get("ProgramFiles(x86)", ""))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe",

        Path(os.environ.get("ProgramFiles", ""))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe",
    ]

    vswhere = next(
        (path for path in candidates if path.exists()),
        None,
    )

    if vswhere is None:
        raise RuntimeError(
            "cl.exe was not found and vswhere.exe could not be located.\n"
            "Install Visual Studio with the Desktop development with C++ "
            "workload."
        )

    query = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
        check=True,
    )

    installation_path = query.stdout.strip()

    if not installation_path:
        raise RuntimeError(
            "Visual Studio with the C++ workload was not found."
        )

    vsdevcmd = (
        Path(installation_path)
        / "Common7"
        / "Tools"
        / "VsDevCmd.bat"
    )

    if not vsdevcmd.exists():
        raise RuntimeError(
            f"Visual Studio developer command script was not found:\n"
            f"  {vsdevcmd}"
        )

    # Use cmd's shell mode here: passing this through a quoted `/s /c`
    # argument list can change how cmd parses the quoted VsDevCmd path.
    command = f'call "{vsdevcmd}" -arch=x64 -host_arch=x64 && set'
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        diagnostic = "\n".join(
            (result.stdout + "\n" + result.stderr).splitlines()[-30:]
        ).strip()
        raise RuntimeError(
            "Visual Studio developer environment setup failed."
            + (f"\n{diagnostic}" if diagnostic else "")
            + "\nOpen a Visual Studio Developer Command Prompt and rerun setup."
        )

    env = os.environ.copy()

    for line in result.stdout.splitlines():
        if "=" not in line:
            continue

        key, value = line.split("=", 1)

        if key:
            env[key] = value

    print("Using Visual Studio developer environment.")
    return env


def command_all() -> None:
    check_submodules()

    print("=== format ===")
    format_files(check=True)

    print("=== debug ===")
    build("debug")

    print("=== release ===")
    build("release")

    print("=== clang-tidy ===")
    lint()

    system = platform.system()

    if system in {"Linux", "Darwin"}:
        print("=== asan ===")
        sanitizer("asan")

        print("=== ubsan ===")
        sanitizer("ubsan")

    print("=== test ===")
    test("debug")

    print()
    print("CI passed.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run project CI checks locally."
    )

    subparsers = parser.add_subparsers(
        dest="command",
        required=True,
    )

    format_parser = subparsers.add_parser(
        "format",
        help="Check or fix clang-format.",
    )

    format_parser.add_argument(
        "--fix",
        action="store_true",
        help="Format files instead of checking them.",
    )

    build_parser = subparsers.add_parser(
        "build",
        help="Configure and build a CMake preset.",
    )

    build_parser.add_argument(
        "--preset",
        choices=("debug", "release", "clang-tidy", "asan", "ubsan"),
        required=True,
    )

    build_only_parser = subparsers.add_parser(
        "build-only",
        help="Build an already-configured CMake preset without setup steps.",
    )

    build_only_parser.add_argument(
        "--preset",
        choices=("debug", "release", "clang-tidy", "asan", "ubsan"),
        default="debug",
    )

    subparsers.add_parser(
        "lint",
        help="Configure and build with clang-tidy.",
    )

    subparsers.add_parser(
        "asan",
        help="Build with AddressSanitizer.",
    )

    subparsers.add_parser(
        "ubsan",
        help="Build with UndefinedBehaviorSanitizer.",
    )

    test_parser = subparsers.add_parser(
        "test",
        help="Build and run tests.",
    )

    test_parser.add_argument(
        "--preset",
        choices=("debug", "release"),
        default="debug",
    )

    subparsers.add_parser(
        "all",
        help="Run the complete local CI suite.",
    )

    args = parser.parse_args()

    try:
        if args.command not in {"format", "build-only"}:
            check_submodules()

        # Ninja Multi-Config + MSVC needs the VS developer environment when
        # cl.exe isn't already present.
        env = setup_msvc_environment()

        if env is not None:
            os.environ.update(env)

        if args.command == "format":
            format_files(check=not args.fix)

        elif args.command == "build":
            build(args.preset)

        elif args.command == "build-only":
            build_only(args.preset)

        elif args.command == "lint":
            lint()

        elif args.command == "asan":
            sanitizer("asan")

        elif args.command == "ubsan":
            sanitizer("ubsan")

        elif args.command == "test":
            test(args.preset)

        elif args.command == "all":
            command_all()

        return 0

    except subprocess.CalledProcessError as exc:
        print()
        print(
            f"Command failed with exit code {exc.returncode}.",
            file=sys.stderr,
        )
        return exc.returncode

    except RuntimeError as exc:
        return die(str(exc))


if __name__ == "__main__":
    sys.exit(main())
