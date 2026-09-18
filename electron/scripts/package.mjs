import { readFile, lstat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { build, Platform, Arch } from 'electron-builder';

const appRoot = fileURLToPath(new URL('..', import.meta.url));
const projectRoot = path.resolve(appRoot, '..');
const version = (await readFile(path.join(projectRoot, 'VERSION'), 'utf8')).trim();
if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error('Root VERSION must use MAJOR.MINOR.PATCH');
const output = path.join(projectRoot, 'build_electron', 'staging');
for (const directory of [path.dirname(output), output]) {
  const info = await lstat(directory).catch(error => {
    if (error.code === 'ENOENT') return undefined;
    throw error;
  });
  if (info?.isSymbolicLink()) throw new Error(`Refusing to package into redirected directory: ${directory}`);
}

await build({
  projectDir: appRoot,
  targets: Platform.WINDOWS.createTarget('dir', Arch.x64),
  config: {
    appId: 'local.multisender.electron',
    productName: 'MultiSender Electron',
    executableName: 'MultiSender Electron',
    extraMetadata: { version },
    directories: { output },
    files: ['dist/**/*', 'package.json'],
    extraResources: [
      { from: path.join(projectRoot, 'VERSION'), to: 'VERSION' },
      { from: path.join(projectRoot, 'CHANGELOG.md'), to: 'CHANGELOG.md' },
      { from: path.join(appRoot, 'assets', 'icon.png'), to: 'icon.png' },
    ],
    asar: true,
    asarUnpack: ['**/*.node', 'node_modules/@serialport/bindings-cpp/prebuilds/**/*'],
    // SerialPort ships Node-API prebuilds, also usable by Electron on Windows x64.
    npmRebuild: false,
    win: { icon: path.join(appRoot, 'assets', 'icon.ico'), signAndEditExecutable: true },
    forceCodeSigning: false,
  },
});
