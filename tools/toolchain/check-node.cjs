'use strict';

const { execFileSync } = require('node:child_process');
const lock = require('../../package.json');

try {
  if (process.versions.node !== lock.engines.node ||
      execFileSync('npm', ['--version'], { encoding: 'utf8', timeout: 10000 }).trim() !== lock.engines.npm ||
      require('typescript/package.json').version !== lock.devDependencies.typescript) {
    throw new Error('version mismatch');
  }
  console.log('Node/npm/TypeScript lock verified');
} catch {
  console.error('Node/npm/TypeScript differ from package.json; select the locked Node/npm and run npm ci');
  process.exitCode = 1;
}
