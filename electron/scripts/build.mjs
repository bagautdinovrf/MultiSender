import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { build as buildRenderer } from 'vite';
import { build as buildMain } from 'esbuild';

const appRoot = fileURLToPath(new URL('..', import.meta.url));
const version = (await readFile(path.join(appRoot, '..', 'VERSION'), 'utf8')).trim();
if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error('Root VERSION must use MAJOR.MINOR.PATCH');

await buildRenderer({ root: appRoot });
await buildMain({
  absWorkingDir: appRoot,
  entryPoints: ['main/main.ts', 'main/preload.ts'],
  outdir: 'dist/main',
  outExtension: { '.js': '.cjs' },
  platform: 'node',
  format: 'cjs',
  target: 'node22',
  bundle: true,
  external: ['electron', 'serialport'],
  define: { __APP_VERSION__: JSON.stringify(version) },
  logLevel: 'info',
});

