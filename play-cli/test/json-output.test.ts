/**
 * The JSON serializer.
 *
 * `JSON.stringify` is wrong on three counts CPython cares about — key order,
 * separators and `ensure_ascii` — and each of those is a byte diff on the
 * offline oracle, so each gets an assertion.
 */
import { describe, expect, test } from 'bun:test';
import {
  canonicalJson,
  compactJson,
  encodeStringAscii,
  indentedJson,
  jsonEnvironment,
  jsonRequested,
  pyJsonDumps,
} from 'src/services/json-output';

describe('pyJsonDumps', () => {
  test('compact form matches json.dumps(sort_keys=True, separators=(",", ":"))', () => {
    expect(compactJson({ b: 1, a: [1, 2], c: { z: null, y: true } })).toBe(
      '{"a":[1,2],"b":1,"c":{"y":true,"z":null}}'
    );
  });

  test('indented form matches json.dumps(indent=2, sort_keys=True)', () => {
    expect(indentedJson({ b: 1, a: { c: 2 } })).toBe(
      ['{', '  "a": {', '    "c": 2', '  },', '  "b": 1', '}'].join('\n')
    );
  });

  test('empty containers collapse the way CPython collapses them', () => {
    expect(indentedJson({ a: {}, b: [] })).toBe(
      ['{', '  "a": {},', '  "b": []', '}'].join('\n')
    );
  });

  test('ensure_ascii escapes non-ASCII by default', () => {
    expect(compactJson({ name: 'Zürich' })).toBe('{"name":"Z\\u00fcrich"}');
  });

  test('astral characters become an explicit surrogate pair', () => {
    expect(encodeStringAscii('\u{1F600}')).toBe('"\\ud83d\\ude00"');
  });

  test('the canonical body keeps non-ASCII literal, as _canonical_body does', () => {
    expect(canonicalJson({ name: 'Zürich' })).toBe('{"name":"Zürich"}');
  });

  test('control characters use the short escapes CPython uses', () => {
    // Written with `fromCharCode` so the control byte is visible in review
    // rather than invisible in the source.
    const control = '\n\t' + String.fromCharCode(1);
    expect(compactJson({ a: control })).toBe('{"a":"\\n\\t\\u0001"}');
  });

  test('the default separators are CPython defaults, not compact ones', () => {
    expect(pyJsonDumps({ a: 1, b: 2 })).toBe('{"a": 1, "b": 2}');
  });
});

describe('jsonRequested', () => {
  test('the JSON-only commands are always JSON', () => {
    expect(jsonRequested('next', false, {})).toBe(true);
    expect(jsonRequested('act', false, {})).toBe(true);
    expect(jsonRequested('result', false, {})).toBe(true);
  });

  test('a text command needs the flag or the environment', () => {
    expect(jsonRequested('state', false, {})).toBe(false);
    expect(jsonRequested('state', true, {})).toBe(true);
    expect(jsonRequested('state', false, { PLAY_JSON: 'yes' })).toBe(true);
  });

  test('PLAY_JSON accepts exactly the four true spellings, case-folded', () => {
    for (const spelling of ['1', 'on', 'true', 'yes', 'TRUE', ' Yes ']) {
      expect(jsonEnvironment({ PLAY_JSON: spelling })).toBe(true);
    }
    for (const spelling of ['0', 'off', 'no', '', 'maybe']) {
      expect(jsonEnvironment({ PLAY_JSON: spelling })).toBe(false);
    }
  });
});
