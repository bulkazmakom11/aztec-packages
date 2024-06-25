import { generateCode } from '@aztec/builder';
import { Logger } from '@aztec/foundation/log';

import { spawn } from 'child_process';
import crypto from 'crypto';
import { createReadStream } from 'fs';
import { rm, stat } from 'fs/promises';
import { dirname } from 'path';
import { resolve } from 'path';
import { fileURLToPath } from 'url';
import which from 'which';

const __dirname = dirname(fileURLToPath(import.meta.url));

export class NoirCompiler {
  private TARGET_PATH: string;

  constructor(private logger: Logger) {
    this.TARGET_PATH = resolve(__dirname, './target');
  }

  async clearCache() {
    await rm(`${this.TARGET_PATH}/*`, { recursive: true });
  }

  async getNargoPath(): Promise<string | null> {
    const aztecNargo = await which('aztec-nargo', { nothrow: true });
    const nargo = await which('nargo', { nothrow: true });
    const compiler = aztecNargo || nargo;
    this.logger.info(`Compiling using ${compiler}`);
    return compiler;
  }

  async #compileUsingNargo(path: string) {
    const nargoPath = await this.getNargoPath();
    if (!nargoPath) {
      throw new Error('Nargo not found');
    }
    const cwd = resolve(path);
    this.logger.info(`Compiling ${cwd} using ${nargoPath}`);
    return new Promise<void>((resolve, reject) => {
      const compiler = spawn(`${nargoPath}`, ['compile', '--silence-warnings'], { cwd });
      compiler.stdout.on('data', data => {
        this.logger.debug(data);
      });

      compiler.stderr.on('data', data => {
        this.logger.error(data);
      });

      compiler.on('close', code => {
        if (code != 0) {
          reject(new Error(`Compilation failed with code ${code}`));
        } else {
          resolve();
        }
      });
    });
  }

  async compile(path: string) {
    await this.#compileUsingNargo(path);
    generateCode(`${this.TARGET_PATH}`, path);

    const tsArtifactPath = `${this.TARGET_PATH}`;
    this.logger.info(`Importing ${tsArtifactPath}`);
    const contractModule = await import(tsArtifactPath!);
    // Hacky way of getting the class, the name of the Artifact is always longer
    const contractClass = contractModule[Object.keys(contractModule).sort((a, b) => a.length - b.length)[0]];
  }
}
