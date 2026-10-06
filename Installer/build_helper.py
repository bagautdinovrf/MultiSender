#!/usr/bin/env python3
"""Build and optionally deploy MultiSender Widgets.

Run from any directory:

    py -3 Installer/build_helper.py 6.11.2 msvc2022_64 --clean --deploy
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from uuid import uuid4


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.resolve()
BUILD_ROOT = (PROJECT_ROOT / "build_agent").resolve()
BUILD_DIR = BUILD_ROOT / "build"
PACKAGE_DIR = BUILD_ROOT / "package"
APP_NAME = "MultiSender.exe"
OBSOLETE_BINARIES = ("MultiSenderQml.exe", "MultiSenderQml.pdb")
RELEASE_FILES = ("VERSION", "CHANGELOG.md")
USER_SETTINGS_FILES = (
    "MultiSender.ini",
    "MultiSender.workspace.json",
    "MultiSender.history.json",
    # Preserve data from the retired QML application, including on --clean.
    "MultiSenderQml.ini",
    "MultiSenderQml.workspace.json",
    "MultiSenderQml.history.json",
    "HttpRequester.ini",
    "HttpRequester.workspace.json",
    "HttpRequester.history.json",
)


def find_vcvars64() -> Path:
    for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
        candidate = Path(
            f"C:/Program Files/Microsoft Visual Studio/2022/{edition}"
            "/VC/Auxiliary/Build/vcvars64.bat"
        )
        if candidate.exists():
            return candidate
    raise FileNotFoundError("Visual Studio 2022 vcvars64.bat was not found")


def configure_environment(qt_version: str, compiler: str) -> tuple[Path, dict[str, str]]:
    qt_root = Path(f"C:/Qt/{qt_version}/{compiler}")
    qtenv = qt_root / "bin" / "qtenv2.bat"
    qt_config = qt_root / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake"
    if not qtenv.exists() or not qt_config.exists():
        raise FileNotFoundError(f"Qt CMake kit is incomplete or missing: {qt_root}")

    environment_marker = f"__AGENT_ENV_CAPTURE_{uuid4().hex}__"
    command_parts = [f'call "{qtenv}"']
    if compiler.lower().startswith("msvc"):
        command_parts.append(f'call "{find_vcvars64()}"')
    command_parts.extend(["echo(", f"echo {environment_marker}", "set"])

    result = subprocess.run(
        " && ".join(command_parts),
        shell=True,
        text=True,
        encoding="cp866",
        errors="replace",
        stdout=subprocess.PIPE,
    )
    lines = result.stdout.splitlines(keepends=True)
    marker_index = next(
        (index for index, line in enumerate(lines) if line.strip() == environment_marker),
        None,
    )
    # Keep the batch scripts' console output separate from environment values.
    diagnostic_stdout = "".join(lines if marker_index is None else lines[:marker_index])
    print(diagnostic_stdout, end="", flush=True)
    if result.returncode != 0 or marker_index is None:
        raise RuntimeError("Failed to initialize the Qt/compiler environment")

    environment = os.environ.copy()
    for line in lines[marker_index + 1:]:
        name, separator, value = line.rstrip("\r\n").partition("=")
        if not name or not separator:
            continue
        if name.upper() == "PATH":
            for existing_name in list(environment):
                if existing_name.upper() == "PATH":
                    del environment[existing_name]
            name = "PATH"
        environment[name] = value
    environment["QTDIR"] = str(qt_root)
    return qt_root, environment


def find_tool(name: str, bundled_path: Path, environment: dict[str, str]) -> Path:
    if bundled_path.exists():
        return bundled_path
    location = shutil.which(name, path=environment.get("PATH", ""))
    if location is None:
        raise FileNotFoundError(f"Required build tool was not found: {name}")
    return Path(location)


def run(command: list[str], cwd: Path, environment: dict[str, str]) -> None:
    print(f"\n[BUILD-HELPER] {subprocess.list2cmdline(command)}", flush=True)
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def validate_build_paths() -> None:
    if BUILD_ROOT.resolve() != PROJECT_ROOT.resolve() / "build_agent":
        raise RuntimeError(f"Refusing to remove unexpected path: {BUILD_ROOT}")
    if PACKAGE_DIR.resolve() != BUILD_ROOT.resolve() / "package":
        raise RuntimeError(f"Refusing to modify unexpected package path: {PACKAGE_DIR}")


def backup_user_settings() -> Path | None:
    validate_build_paths()
    existing_files = [
        PACKAGE_DIR / name for name in USER_SETTINGS_FILES
        if (PACKAGE_DIR / name).exists()
    ]
    if not existing_files:
        return None

    backup_dir = Path(tempfile.mkdtemp(prefix="multisender_settings_"))
    if backup_dir.resolve().is_relative_to(BUILD_ROOT.resolve()):
        backup_dir.rmdir()
        raise RuntimeError("Settings backup directory must be outside build_agent")
    print(f"[BUILD-HELPER] Settings backup: {backup_dir}", flush=True)
    try:
        for source in existing_files:
            shutil.copy2(source, backup_dir / source.name)
    except BaseException:
        print(
            f"[BUILD-HELPER] Incomplete settings backup retained: {backup_dir}",
            file=sys.stderr,
            flush=True,
        )
        raise
    return backup_dir


def restore_user_settings(backup_dir: Path) -> None:
    validate_build_paths()
    PACKAGE_DIR.mkdir(parents=True, exist_ok=True)
    for name in USER_SETTINGS_FILES:
        source = backup_dir / name
        if source.exists():
            shutil.copy2(source, PACKAGE_DIR / name)
    print(f"[BUILD-HELPER] User settings restored from {backup_dir}", flush=True)
    for name in USER_SETTINGS_FILES:
        (backup_dir / name).unlink(missing_ok=True)
    backup_dir.rmdir()


def safe_clean_build_root() -> None:
    validate_build_paths()
    if BUILD_ROOT.exists():
        print(f"[BUILD-HELPER] Removing {BUILD_ROOT}", flush=True)
        shutil.rmtree(BUILD_ROOT)


def configure_and_build(
    cmake: Path,
    ninja: Path,
    qt_root: Path,
    configuration: str,
    environment: dict[str, str],
) -> Path:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    PACKAGE_DIR.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(cmake),
            "-S", str(PROJECT_ROOT),
            "-B", str(BUILD_DIR),
            "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_PREFIX_PATH={qt_root}",
            f"-DCMAKE_BUILD_TYPE={configuration}",
            "-DBUILD_TESTING=OFF",
            f"-DMULTISENDER_PACKAGE_DIRECTORY={PACKAGE_DIR}",
        ],
        PROJECT_ROOT,
        environment,
    )
    run(
        [str(cmake), "--build", str(BUILD_DIR), "--parallel"],
        PROJECT_ROOT,
        environment,
    )

    executable = PACKAGE_DIR / APP_NAME
    if not executable.exists():
        raise FileNotFoundError(f"Built executable was not found: {executable}")
    return executable


def deploy_application(
    qt_root: Path,
    executable: Path,
    configuration: str,
    environment: dict[str, str],
) -> Path:
    validate_build_paths()
    executable = executable.resolve()
    if executable != PACKAGE_DIR.resolve() / APP_NAME:
        raise RuntimeError(f"Refusing to deploy unexpected executable: {executable}")
    # Incremental deployment only adds or updates runtime dependencies.
    # A full cleanup is explicitly handled by --clean before the build.

    deploy_tool = qt_root / "bin" / "windeployqt.exe"
    mode_flag = "--release" if configuration == "Release" else "--debug"
    run(
        [str(deploy_tool), mode_flag, "--compiler-runtime", str(executable)],
        PACKAGE_DIR,
        environment,
    )
    return PACKAGE_DIR


def remove_obsolete_binaries() -> None:
    validate_build_paths()
    for name in OBSOLETE_BINARIES:
        obsolete = PACKAGE_DIR / name
        if obsolete.resolve().parent != PACKAGE_DIR.resolve():
            raise RuntimeError(f"Refusing to remove unexpected binary: {obsolete}")
        if obsolete.exists():
            obsolete.unlink()
            print(f"[BUILD-HELPER] Removed obsolete binary: {obsolete}", flush=True)


def copy_release_metadata() -> None:
    validate_build_paths()
    for name in RELEASE_FILES:
        shutil.copy2(PROJECT_ROOT / name, PACKAGE_DIR / name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt_version", help="Qt directory name, for example 6.11.2")
    parser.add_argument("compiler", help="Qt compiler kit, for example msvc2022_64")
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--clean", action="store_true", help="Clean the Qt build folder after backing up settings")
    parser.add_argument(
        "--deploy",
        action="store_true",
        help="Create a runnable package with all runtime dependencies",
    )
    args = parser.parse_args()

    qt_root, environment = configure_environment(args.qt_version, args.compiler)
    cmake = find_tool(
        "cmake", Path("C:/Qt/Tools/CMake_64/bin/cmake.exe"), environment
    )
    ninja = find_tool(
        "ninja", Path("C:/Qt/Tools/Ninja/ninja.exe"), environment
    )

    print(f"[BUILD-HELPER] Project: {PROJECT_ROOT}")
    print(f"[BUILD-HELPER] Version: {(PROJECT_ROOT / 'VERSION').read_text(encoding='utf-8').strip()}")
    print(f"[BUILD-HELPER] Qt kit: {qt_root}")
    print(f"[BUILD-HELPER] CMake: {cmake}")
    print("[BUILD-HELPER] Generator: Ninja")
    print(f"[BUILD-HELPER] Configuration: {args.config}")

    backup_dir = backup_user_settings() if args.clean or args.deploy else None
    try:
        if args.clean:
            safe_clean_build_root()
        validate_build_paths()
        BUILD_ROOT.mkdir(exist_ok=True)

        executable = configure_and_build(
            cmake, ninja, qt_root, args.config, environment
        )
        if args.deploy:
            package_dir = deploy_application(
                qt_root, executable, args.config, environment
            )
        copy_release_metadata()
        remove_obsolete_binaries()
        if backup_dir is not None:
            restore_user_settings(backup_dir)
            backup_dir = None
        print(f"\n[BUILD-HELPER] BUILD SUCCESS: {executable}")
        if args.deploy:
            print(f"[BUILD-HELPER] RUNNABLE PACKAGE: {package_dir}")
    except BaseException:
        if backup_dir is not None:
            print(
                f"[BUILD-HELPER] Settings backup retained: {backup_dir}",
                file=sys.stderr,
                flush=True,
            )
        raise
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"\n[BUILD-HELPER] FAILED: {error}", file=sys.stderr)
        raise SystemExit(1)
