#!/usr/bin/env python3
"""Build and optionally deploy MultiSender Widgets or its separate Electron copy.

Run from any directory:

    py -3 Installer/build_helper.py 6.11.2 msvc2022_64 --clean --deploy
    py -3 Installer/build_helper.py --electron --deploy
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


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
ELECTRON_ROOT = PROJECT_ROOT / "electron"
ELECTRON_BUILD_ROOT = PROJECT_ROOT / "build_electron"
ELECTRON_PACKAGE_DIR = ELECTRON_BUILD_ROOT / "package"
ELECTRON_SETTINGS_FILES = (
    "MultiSenderElectron.settings.json",
    "MultiSenderElectron.workspace.json",
    "MultiSenderElectron.history.json",
    *USER_SETTINGS_FILES,
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

    capture_handle, capture_name = tempfile.mkstemp(
        prefix="multisender_env_", suffix=".txt"
    )
    os.close(capture_handle)
    capture_file = Path(capture_name)
    command_parts = [f'call "{qtenv}"']
    if compiler.lower().startswith("msvc"):
        command_parts.append(f'call "{find_vcvars64()}"')
    command_parts.append(f'set > "{capture_file}"')

    try:
        result = subprocess.run(
            " && ".join(command_parts),
            shell=True,
            text=True,
            encoding="cp866",
            errors="replace",
        )
        if result.returncode != 0:
            raise RuntimeError("Failed to initialize the Qt/compiler environment")

        environment = os.environ.copy()
        for line in capture_file.read_text(encoding="cp866", errors="ignore").splitlines():
            if "=" not in line:
                continue
            name, value = line.split("=", 1)
            if name.upper() == "PATH":
                for existing_name in list(environment):
                    if existing_name.upper() == "PATH":
                        del environment[existing_name]
                name = "PATH"
            environment[name] = value
        environment["QTDIR"] = str(qt_root)
        return qt_root, environment
    finally:
        capture_file.unlink(missing_ok=True)


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


def validate_electron_paths() -> None:
    expected_root = PROJECT_ROOT.resolve() / "build_electron"
    if ELECTRON_BUILD_ROOT.resolve() != expected_root:
        raise RuntimeError(f"Refusing to modify unexpected path: {ELECTRON_BUILD_ROOT}")
    for name in ("package", "staging"):
        target = ELECTRON_BUILD_ROOT / name
        if target.resolve() != expected_root / name:
            raise RuntimeError(f"Refusing to modify redirected directory: {target}")


def backup_electron_settings() -> Path | None:
    validate_electron_paths()
    existing = [
        ELECTRON_PACKAGE_DIR / name for name in ELECTRON_SETTINGS_FILES
        if (ELECTRON_PACKAGE_DIR / name).exists()
    ]
    if not existing:
        return None
    backup = Path(tempfile.mkdtemp(prefix="multisender_electron_settings_"))
    if backup.resolve().is_relative_to(ELECTRON_BUILD_ROOT.resolve()):
        backup.rmdir()
        raise RuntimeError("Electron settings backup must be outside build_electron")
    print(f"[BUILD-HELPER] Electron settings backup: {backup}", flush=True)
    try:
        for source in existing:
            shutil.copy2(source, backup / source.name)
    except BaseException:
        print(f"[BUILD-HELPER] Incomplete settings backup retained: {backup}",
              file=sys.stderr, flush=True)
        raise
    return backup


def restore_electron_settings(backup: Path) -> None:
    validate_electron_paths()
    ELECTRON_PACKAGE_DIR.mkdir(parents=True, exist_ok=True)
    for name in ELECTRON_SETTINGS_FILES:
        source = backup / name
        if source.exists():
            shutil.copy2(source, ELECTRON_PACKAGE_DIR / name)
    print(f"[BUILD-HELPER] Electron settings restored from {backup}", flush=True)
    for name in ELECTRON_SETTINGS_FILES:
        (backup / name).unlink(missing_ok=True)
    backup.rmdir()


def electron_node_environment(node_argument: str | None) -> tuple[list[str], dict[str, str]]:
    node_location = node_argument or os.environ.get("NODE_EXE") or shutil.which("node")
    if not node_location:
        raise FileNotFoundError("Node.js >=22.12 is required; install it or pass --node PATH")
    node = Path(node_location).resolve()
    version = subprocess.check_output([str(node), "--version"], text=True).strip()
    parts = tuple(int(value) for value in version.lstrip("v").split(".")[:2])
    if parts < (22, 12):
        raise RuntimeError(f"Node.js >=22.12 is required, found {version}; pass --node PATH")
    environment = os.environ.copy()
    path_key = next((key for key in environment if key.upper() == "PATH"), "PATH")
    environment[path_key] = str(node.parent) + os.pathsep + environment.get(path_key, "")
    candidates = [
        node.parent / "node_modules" / "npm" / "bin" / "npm-cli.js",
        node.parent.parent / "lib" / "node_modules" / "npm" / "bin" / "npm-cli.js",
        node.parent.parent / "node_modules" / "npm" / "bin" / "npm-cli.js",
    ]
    # A bundled Node runtime can use npm supplied by a normal system Node install.
    npm_location = shutil.which("npm.cmd" if os.name == "nt" else "npm")
    if npm_location:
        npm_path = Path(npm_location).resolve()
        candidates.extend([
            npm_path.parent / "node_modules" / "npm" / "bin" / "npm-cli.js",
            npm_path,
        ] if os.name != "nt" else [
            npm_path.parent / "node_modules" / "npm" / "bin" / "npm-cli.js",
        ])
    npm_cli = next((candidate for candidate in candidates if candidate.is_file()), None)
    if npm_cli is None:
        raise FileNotFoundError("npm was not found; install a Node.js distribution including npm")
    print(f"[BUILD-HELPER] Node: {node} ({version})", flush=True)
    return [str(node), str(npm_cli)], environment


def build_electron(args: argparse.Namespace) -> int:
    npm, environment = electron_node_environment(args.node)
    validate_electron_paths()
    backup = backup_electron_settings() if args.clean or args.deploy else None
    try:
        if args.clean and ELECTRON_BUILD_ROOT.exists():
            print(f"[BUILD-HELPER] Removing {ELECTRON_BUILD_ROOT}", flush=True)
            shutil.rmtree(ELECTRON_BUILD_ROOT)
        ELECTRON_BUILD_ROOT.mkdir(parents=True, exist_ok=True)
        if args.install or not (ELECTRON_ROOT / "node_modules").is_dir():
            install = "ci" if (ELECTRON_ROOT / "package-lock.json").exists() else "install"
            run([*npm, install], ELECTRON_ROOT, environment)
        run([*npm, "run", "typecheck"], ELECTRON_ROOT, environment)
        run([*npm, "run", "build"], ELECTRON_ROOT, environment)
        if args.deploy:
            run([*npm, "run", "package"], ELECTRON_ROOT, environment)
            validate_electron_paths()
            staged = ELECTRON_BUILD_ROOT / "staging" / "win-unpacked"
            executable_name = "MultiSender Electron.exe"
            if not (staged / executable_name).is_file():
                raise FileNotFoundError(f"Packaged Electron executable was not found: {staged}")
            # Never delete the existing package during incremental deployment.
            # The builder only writes into staging, where there are no user files.
            shutil.copytree(staged, ELECTRON_PACKAGE_DIR, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns(*ELECTRON_SETTINGS_FILES))
            for name in RELEASE_FILES:
                shutil.copy2(PROJECT_ROOT / name, ELECTRON_PACKAGE_DIR / name)
            shutil.copy2(ELECTRON_ROOT / "README.md", ELECTRON_PACKAGE_DIR / "README-Electron.md")
        if backup is not None:
            restore_electron_settings(backup)
            backup = None
        if args.zip:
            version = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8").strip()
            # Zip the clean staging package so no user workspace or history is shared.
            for name in RELEASE_FILES:
                shutil.copy2(PROJECT_ROOT / name, staged / name)
            shutil.copy2(ELECTRON_ROOT / "README.md", staged / "README-Electron.md")
            archive = shutil.make_archive(
                str(ELECTRON_BUILD_ROOT / f"MultiSender-Electron-{version}-win-x64"),
                "zip", root_dir=staged,
            )
            print(f"[BUILD-HELPER] DISTRIBUTION ZIP: {archive}", flush=True)
        if args.deploy:
            print(f"[BUILD-HELPER] RUNNABLE PACKAGE: {ELECTRON_PACKAGE_DIR}", flush=True)
            print(f"[BUILD-HELPER] RUN: {ELECTRON_PACKAGE_DIR / executable_name}", flush=True)
        else:
            print(f"[BUILD-HELPER] ELECTRON BUILD SUCCESS: {ELECTRON_ROOT / 'dist'}", flush=True)
    except BaseException:
        if backup is not None:
            print(f"[BUILD-HELPER] Electron settings backup retained: {backup}",
                  file=sys.stderr, flush=True)
        raise
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt_version", nargs="?", help="Qt directory name, for example 6.11.2")
    parser.add_argument("compiler", nargs="?", help="Qt compiler kit, for example msvc2022_64")
    parser.add_argument("--electron", action="store_true", help="Build the separate Electron application")
    parser.add_argument("--node", help="Node.js executable for --electron (defaults to NODE_EXE or PATH)")
    parser.add_argument("--install", action="store_true", help="Install Electron dependencies from lockfile")
    parser.add_argument("--zip", action="store_true", help="Also create a clean Electron ZIP (requires --deploy)")
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--clean", action="store_true", help="Clean selected build folder after backing up settings")
    parser.add_argument(
        "--deploy",
        action="store_true",
        help="Create a runnable package with all runtime dependencies",
    )
    args = parser.parse_args()

    if args.electron:
        if args.qt_version or args.compiler:
            parser.error("Qt arguments cannot be used with --electron")
        if args.zip and not args.deploy:
            parser.error("--zip requires --deploy")
        return build_electron(args)
    if not args.qt_version or not args.compiler:
        parser.error("qt_version and compiler are required unless --electron is selected")
    if args.node or args.install or args.zip:
        parser.error("--node, --install and --zip require --electron")

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
