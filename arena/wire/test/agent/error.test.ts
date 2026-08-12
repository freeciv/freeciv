/**
 * The structured error envelope: the closed code vocabulary, the message
 * bounds, the required-and-nullable `state_revision`, and the rendering
 * helpers that keep `.strip()` out of the decoder.
 */

import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  ACTION_OUTCOME_AMBIGUOUS,
  decodeError,
  ERROR_MESSAGE_MAX,
  errorMessageText,
  StructuredError,
  V2_ERROR_CODES,
  v2ErrorMessage,
} from 'src/agent/error';
import { encodeTolerant } from 'src/tolerant';
import { errorEnvelope, REVISION } from 'test/agent/wire-fixtures';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const withBody = (body: Record<string, unknown>): unknown => ({
  ...errorEnvelope(),
  error: { code: 'invalid_request', message: 'validated test error', retryable: false, details: {}, ...body },
});

describe('the code vocabulary', () => {
  test('is the fourteen codes play/client.py:56 lists, in order', () => {
    expect(V2_ERROR_CODES).toEqual([
      'action_expired',
      'action_outcome_ambiguous',
      'conflict',
      'cursor_expired',
      'illegal_action',
      'internal_error',
      'invalid_batch',
      'invalid_request',
      'not_implemented',
      'rate_limited',
      'scope_too_large',
      'sidecar_unavailable',
      'stale_revision',
      'unsupported_protocol',
    ]);
    expect(V2_ERROR_CODES).toContain(ACTION_OUTCOME_AMBIGUOUS);
  });

  test('every listed code decodes', () => {
    for (const code of V2_ERROR_CODES) {
      expect(accepts(decodeError(errorEnvelope(code)))).toBe(true);
    }
  });

  test('is closed — an unknown code is a protocol change, not a payload to guess at', () => {
    expect(accepts(decodeError(errorEnvelope('teapot')))).toBe(false);
  });
});

describe('the envelope', () => {
  test('decodes with a revision and without one', () => {
    expect(accepts(decodeError(errorEnvelope('conflict', REVISION)))).toBe(true);
    expect(accepts(decodeError(errorEnvelope('conflict', null)))).toBe(true);
  });

  test('refuses an envelope that omits state_revision — parity with _exact', () => {
    const { state_revision: _dropped, ...withoutRevision } = errorEnvelope();
    expect(accepts(decodeError(withoutRevision))).toBe(false);
  });

  test('pins the two header literals', () => {
    expect(accepts(decodeError({ ...errorEnvelope(), schema_version: 1 }))).toBe(false);
    expect(accepts(decodeError({ ...errorEnvelope(), control_protocol: 'strategic-v1' }))).toBe(
      false,
    );
  });

  test('retryable must be a real boolean', () => {
    expect(accepts(decodeError(withBody({ retryable: 'no' })))).toBe(false);
  });

  test('details is open but bounded', () => {
    expect(accepts(decodeError(withBody({ details: { safe_next: 'receipt_first' } })))).toBe(true);
    expect(accepts(decodeError(withBody({ details: [] })))).toBe(false);
  });
});

describe('the message', () => {
  test('may not be blank or whitespace-only', () => {
    expect(accepts(decodeError(withBody({ message: '' })))).toBe(false);
    expect(accepts(decodeError(withBody({ message: '   ' })))).toBe(false);
  });

  test(`is capped at ${String(ERROR_MESSAGE_MAX)} characters`, () => {
    expect(accepts(decodeError(withBody({ message: 'x'.repeat(ERROR_MESSAGE_MAX) })))).toBe(true);
    expect(accepts(decodeError(withBody({ message: 'x'.repeat(ERROR_MESSAGE_MAX + 1) })))).toBe(
      false,
    );
  });

  test('is measured in code points, the way Python len() measures it', () => {
    // 300 astral characters: 300 to Python, 600 to String#length.
    expect(accepts(decodeError(withBody({ message: '𝔘'.repeat(300) })))).toBe(true);
  });

  test('keeps its bytes; the trim lives in errorMessageText', () => {
    const decoded = decodeError(withBody({ message: '  spaced  ' }));
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    expect(decoded.right.error.message).toBe('  spaced  ');
    expect(errorMessageText(decoded.right)).toBe('spaced');
    expect(v2ErrorMessage(409, decoded.right)).toBe('HTTP 409: spaced (invalid_request)');
  });
});

describe('round trip', () => {
  test('an unknown field survives decode and re-encode, in place', () => {
    const wire = { ...errorEnvelope(), trace_id: 'abc' };
    const decoded = decodeError(wire);
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    const encoded = encodeTolerant(StructuredError)(decoded.right);
    expect(Either.isRight(encoded)).toBe(true);
    if (!Either.isRight(encoded)) return;
    expect(JSON.stringify(encoded.right)).toBe(JSON.stringify(wire));
  });
});
