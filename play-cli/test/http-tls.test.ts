/**
 * Private-CA trust (NOTES §I.3.1): `caTrustedFetch` resolves the CA per
 * request from PLAY_TLS_CA or the workspace `.playconfig.json`, and a
 * configured-but-unreadable CA refuses with the path in the message instead of
 * degrading to the untrusted default.
 */
import { afterEach, describe, expect, test } from 'bun:test';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { caTrustedFetch } from 'src/services/http';

interface Captured {
  init: RequestInit | undefined;
}

const capturing = (): { fetchImpl: typeof fetch; captured: Captured } => {
  const captured: Captured = { init: undefined };
  const fetchImpl = Object.assign(
    (_input: string | URL | Request, init?: RequestInit): Promise<Response> => {
      captured.init = init;
      return Promise.resolve(new Response('{}'));
    },
    { preconnect: fetch.preconnect }
  );
  return { fetchImpl, captured };
};

const tlsOf = (init: RequestInit | undefined): unknown =>
  (init as { tls?: unknown } | undefined)?.tls;

const scratches: string[] = [];
const savedEnv = {
  PLAY_TLS_CA: process.env['PLAY_TLS_CA'],
  PLAY_ROOT: process.env['PLAY_ROOT'],
};

afterEach(() => {
  for (const [name, value] of Object.entries(savedEnv)) {
    if (value === undefined) {
      delete process.env[name];
    } else {
      process.env[name] = value;
    }
  }
  for (const scratch of scratches.splice(0)) {
    fs.rmSync(scratch, { recursive: true, force: true });
  }
});

const scratchDir = (): string => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'http-tls-'));
  scratches.push(dir);
  return dir;
};

describe('caTrustedFetch', () => {
  test('no configuration leaves the request untouched', async () => {
    delete process.env['PLAY_TLS_CA'];
    process.env['PLAY_ROOT'] = scratchDir(); // no .playconfig.json inside
    const { fetchImpl, captured } = capturing();
    await caTrustedFetch(fetchImpl)('https://supervisor.test/health');
    expect(tlsOf(captured.init)).toBeUndefined();
  });

  test('PLAY_TLS_CA injects the file contents as tls.ca', async () => {
    const dir = scratchDir();
    const caPath = path.join(dir, 'ca.pem');
    fs.writeFileSync(caPath, 'FAKE CA PEM\n');
    process.env['PLAY_TLS_CA'] = caPath;
    const { fetchImpl, captured } = capturing();
    await caTrustedFetch(fetchImpl)('https://supervisor.test/health');
    expect(tlsOf(captured.init)).toEqual({ ca: 'FAKE CA PEM\n' });
  });

  test('the workspace .playconfig.json tls_ca is the fallback, relative to the root', async () => {
    const dir = scratchDir();
    fs.writeFileSync(path.join(dir, 'stack-ca.pem'), 'WORKSPACE CA\n');
    fs.writeFileSync(
      path.join(dir, '.playconfig.json'),
      JSON.stringify({ schema_version: 1, game_id: 'game_x', name: 'n', tls_ca: 'stack-ca.pem' })
    );
    delete process.env['PLAY_TLS_CA'];
    process.env['PLAY_ROOT'] = dir;
    const { fetchImpl, captured } = capturing();
    await caTrustedFetch(fetchImpl)('https://supervisor.test/health');
    expect(tlsOf(captured.init)).toEqual({ ca: 'WORKSPACE CA\n' });
  });

  test('an empty PLAY_TLS_CA opts out of the workspace fallback', async () => {
    const dir = scratchDir();
    fs.writeFileSync(path.join(dir, 'stack-ca.pem'), 'WORKSPACE CA\n');
    fs.writeFileSync(
      path.join(dir, '.playconfig.json'),
      JSON.stringify({ schema_version: 1, game_id: 'game_x', name: 'n', tls_ca: 'stack-ca.pem' })
    );
    process.env['PLAY_TLS_CA'] = '';
    process.env['PLAY_ROOT'] = dir;
    const { fetchImpl, captured } = capturing();
    await caTrustedFetch(fetchImpl)('https://supervisor.test/health');
    expect(tlsOf(captured.init)).toBeUndefined();
  });

  test('a configured but unreadable CA refuses with the path in the message', () => {
    const missing = path.join(scratchDir(), 'gone.pem');
    process.env['PLAY_TLS_CA'] = missing;
    const { fetchImpl } = capturing();
    expect(() => caTrustedFetch(fetchImpl)('https://supervisor.test/health')).toThrow(missing);
  });
});
