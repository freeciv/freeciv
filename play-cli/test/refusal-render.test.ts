/**
 * Refusal rendering — `_render_error_payload` and its three helpers.
 *
 * Ports `test_v2_the_json_remedy_line_is_only_printed_when_it_pays`
 * (test_client.py:9784) and `test_v2_rate_limits_and_retired_cursors_name_a_
 * real_next_step` (9813), including their shared `error_lines` helper.
 *
 * The two rules under test are both about *not* lying to the agent: a remedy is
 * never invented for a page this client cannot spell, and `--json` is never
 * offered when it would add nothing.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import {
  ERROR_REMEDIES,
  RefusalRenderLive,
  errorText,
  renderErrorPayload,
  restartCommand,
  retryAfterText,
} from 'src/render/refusal';
import { RefusalRender } from 'src/render/refusal-seam';
import { decodeError } from 'src/schema/error';
import type { JsonObject } from 'src/schema/primitives';
import { errorPayload, identity } from 'test/_fixtures';

// ---------------------------------------------------------------------------
// The `error_lines` helper, ported (test_client.py:9775-9782)
// ---------------------------------------------------------------------------

const errorLines = (
  code: string,
  details: JsonObject,
  options: { readonly retryable?: boolean; readonly revision?: JsonObject } = {}
): ReadonlyArray<string> =>
  renderErrorPayload({
    error: {
      code,
      message: 'the request could not be completed',
      details,
      retryable: options.retryable ?? false,
    },
    ...(options.revision === undefined ? {} : { state_revision: options.revision }),
  });

const JSON_OFFER = 'full payload: re-run the same command with --json';
const RETRYABLE = 'retryable: the same request may be sent again';
const HEADLINE = 'error illegal_action: the request could not be completed';

// ---------------------------------------------------------------------------
// _error_text
// ---------------------------------------------------------------------------

describe('errorText', () => {
  test('it is exactly "code: message", which a receipt line inlines', () => {
    const error = Effect.runSync(
      decodeError(
        errorPayload({
          error: {
            code: 'illegal_action',
            message: 'validated test error',
            retryable: false,
            details: {},
          },
        })
      )
    );
    expect(errorText(error)).toBe('illegal_action: validated test error');
  });
});

// ---------------------------------------------------------------------------
// _retry_after_text
// ---------------------------------------------------------------------------

describe('retryAfterText', () => {
  test('seconds alone, then seconds plus the wall clock', () => {
    expect(retryAfterText({ retry_after_seconds: 12 })).toBe(
      'retry the same command in 12s'
    );
    expect(
      retryAfterText({ retry_after_seconds: 12, retry_after: '2999-01-01T00:00:12.000Z' })
    ).toBe('retry the same command in 12s (not before 2999-01-01T00:00:12.000Z)');
  });

  test('a fractional wait keeps CPython’s %g, not a float dump', () => {
    expect(retryAfterText({ retry_after_seconds: 0.5 })).toBe(
      'retry the same command in 0.5s'
    );
  });

  test('a bool is not a number, and a missing key is not a remedy', () => {
    expect(retryAfterText({ retry_after_seconds: true })).toBe('');
    expect(retryAfterText({})).toBe('');
    expect(retryAfterText({ retry_after_seconds: '12' })).toBe('');
    // An empty `retry_after` adds no parenthetical rather than an empty one.
    expect(retryAfterText({ retry_after_seconds: 3, retry_after: '' })).toBe(
      'retry the same command in 3s'
    );
  });
});

// ---------------------------------------------------------------------------
// _restart_command
// ---------------------------------------------------------------------------

describe('restartCommand', () => {
  test('it spells the query in the order the recipe takes it', () => {
    expect(
      restartCommand({
        endpoint: 'state',
        // Deliberately out of order on the wire: the output order is the
        // recipe's, not the payload's.
        query: { limit: 16, section: 'known_tiles' },
      })
    ).toBe('just state --section known_tiles --limit 16');
  });

  test('an endpoint this client has no recipe for is withheld', () => {
    expect(restartCommand({ endpoint: 'receipts', query: { limit: 16 } })).toBe('');
  });

  test('one unspellable option withholds the whole command', () => {
    expect(
      restartCommand({
        endpoint: 'state',
        query: { section: 'units', unknown_option: 1 },
      })
    ).toBe('');
  });

  test('an empty or absent query is not a command', () => {
    expect(restartCommand({ endpoint: 'state', query: {} })).toBe('');
    expect(restartCommand({ endpoint: 'state' })).toBe('');
    expect(restartCommand('state')).toBe('');
    expect(restartCommand(null)).toBe('');
  });

  test('a null option is skipped, not spelled as "None"', () => {
    expect(
      restartCommand({ endpoint: 'legal_actions', query: { actor_id: 'unit_1', limit: null } })
    ).toBe('just legal --actor_id unit_1');
  });
});

// ---------------------------------------------------------------------------
// _render_error_payload — the `--json` offer
// ---------------------------------------------------------------------------

describe('renderErrorPayload: the --json offer only when it pays', () => {
  test('an empty details object offers nothing but says it is retryable', () => {
    const lines = errorLines('rate_limited', {}, { retryable: true });
    expect(lines).not.toContain(JSON_OFFER);
    expect(lines).toContain(RETRYABLE);
    expect(lines[0]).toBe('error rate_limited: the request could not be completed');
  });

  test('a scalar detail is printed verbatim, so --json adds nothing', () => {
    const lines = errorLines('illegal_action', {
      safe_next: 'refresh',
      field: 'leader_name',
    });
    expect(lines).toContain('  field=leader_name');
    expect(lines).not.toContain(JSON_OFFER);
  });

  test('a nested detail is the one thing the compact form has to elide', () => {
    const lines = errorLines('illegal_action', {
      safe_next: 'refresh',
      rejected: { argument: { a: 1 } },
    });
    expect(lines).toContain(JSON_OFFER);
  });

  test('a nested detail under an *expressed* key is not an elision', () => {
    // `restart` is spelled out in full above, so nothing was elided and the
    // offer would be a wasted call.
    const lines = errorLines('cursor_expired', {
      restart: { endpoint: 'state', query: { section: 'units' } },
    });
    expect(lines).toContain('next: just state --section units');
    expect(lines).not.toContain(JSON_OFFER);
  });

  test('an unspellable restart elides, so the offer comes back', () => {
    const lines = errorLines('stale_revision', {
      restart: { endpoint: 'state', query: { section: 'units', unknown_option: 1 } },
    });
    expect(lines.filter((line) => line.startsWith('next: just'))).toEqual([]);
    expect(lines).toContain('  restart=…');
    expect(lines).toContain(JSON_OFFER);
  });
});

// ---------------------------------------------------------------------------
// _render_error_payload — the remedy lines
// ---------------------------------------------------------------------------

describe('renderErrorPayload: remedies', () => {
  test('rate limits name a clock, and the flag that cannot help is not offered', () => {
    const lines = errorLines(
      'rate_limited',
      { retry_after_seconds: 12, retry_after: '2999-01-01T00:00:12.000Z' },
      { retryable: true }
    );
    expect(lines).toContain(
      'next: retry the same command in 12s (not before 2999-01-01T00:00:12.000Z)'
    );
    expect(lines).not.toContain(JSON_OFFER);
    expect(lines).not.toContain('  retry_after_seconds=12');
  });

  test('a retired page chain forwards a command that runs as printed', () => {
    expect(
      errorLines('stale_revision', {
        restart: { endpoint: 'state', query: { section: 'known_tiles', limit: 16 } },
      })
    ).toContain('next: just state --section known_tiles --limit 16');
    const actor = `unit_${'a'.repeat(32)}`;
    expect(
      errorLines('cursor_expired', {
        restart: { endpoint: 'legal_actions', query: { actor_id: actor, limit: 16 } },
      })
    ).toContain(`next: just legal --actor_id ${actor} --limit 16`);
  });

  test('every safe_next in the table gets its sentence, and none is invented', () => {
    expect([...ERROR_REMEDIES.keys()].sort()).toEqual([
      'receipt_first',
      'refresh',
      'retry_exact',
    ]);
    for (const [safeNext, remedy] of ERROR_REMEDIES) {
      expect(errorLines('conflict', { safe_next: safeNext })).toContain(
        `next (${safeNext}): ${remedy}`
      );
    }
    // A `safe_next` this client does not know is not guessed at, and the raw
    // value is suppressed too because `safe_next` is always "expressed".
    const unknown = errorLines('conflict', { safe_next: 'reboot_the_universe' });
    expect(unknown.filter((line) => line.startsWith('next'))).toEqual([]);
    expect(unknown).toEqual([HEADLINE.replace('illegal_action', 'conflict')]);
  });

  test('the batch ID is substituted into the remedy, so it runs as printed', () => {
    const batchId = `batch_${'R'.repeat(24)}`;
    const lines = errorLines('conflict', { safe_next: 'receipt_first', batch_id: batchId });
    expect(lines).toContain(
      `next (receipt_first): resolve the outcome with \`just receipt --batch_id ${batchId}\` ` +
        'before any replay'
    );
    // `batch_id` is expressed by the substitution, so it never doubles up in
    // the raw-details tail.
    expect(lines.some((line) => line.includes('batch_id='))).toBe(false);
  });

  test('an empty batch ID leaves the placeholder rather than an empty flag', () => {
    const lines = errorLines('conflict', { safe_next: 'retry_exact', batch_id: '' });
    expect(lines).toContain(
      'next (retry_exact): retry the same batch with `just retry --batch_id ID`'
    );
  });
});

// ---------------------------------------------------------------------------
// _render_error_payload — shape and ordering
// ---------------------------------------------------------------------------

describe('renderErrorPayload: shape', () => {
  test('the headline carries the revision when the payload has one', () => {
    const lines = errorLines(
      'stale_revision',
      {},
      { revision: { turn: 3, revision: 8, state_token: 'token' } }
    );
    expect(lines[0]).toBe('error stale_revision: the request could not be completed  rev8/t3');
  });

  test('a null state_revision adds no suffix', () => {
    expect(errorLines('conflict', {})[0]).toBe(
      'error conflict: the request could not be completed'
    );
  });

  test('the raw-details tail is sorted by key and drops nulls', () => {
    const lines = errorLines('illegal_action', {
      zebra: 1,
      alpha: 'two',
      middle: null,
      flagged: true,
    });
    expect(lines).toContain('  alpha=two flagged=yes zebra=1');
  });

  test('the line order is headline, remedies, raw details, retryable, offer', () => {
    const lines = errorLines(
      'conflict',
      {
        safe_next: 'receipt_first',
        retry_after_seconds: 2,
        restart: { endpoint: 'state', query: { section: 'units' } },
        extra: { nested: true },
      },
      { retryable: true }
    );
    expect(lines).toEqual([
      'error conflict: the request could not be completed',
      'next (receipt_first): resolve the outcome with `just receipt --batch_id ID` before any replay',
      'next: retry the same command in 2s',
      'next: just state --section units',
      '  extra=…',
      RETRYABLE,
      JSON_OFFER,
    ]);
  });

  test('a payload that is not a refusal falls back to the raw value', () => {
    expect(renderErrorPayload(null)).toEqual(['-']);
    expect(renderErrorPayload('boom')).toEqual(['boom']);
    expect(renderErrorPayload({ error: 'boom' })).toEqual(['{"error":"boom"}']);
    expect(renderErrorPayload([1, 2])).toEqual(['[1,2]']);
  });

  test('retryable false is not printed as a line saying so', () => {
    expect(errorLines('illegal_action', {})).toEqual([HEADLINE]);
  });
});

// ---------------------------------------------------------------------------
// The real payload cli-main hands it, and the seam
// ---------------------------------------------------------------------------

describe('the RefusalRender seam', () => {
  test('a validated StructuredError renders through the same path', () => {
    const payload = Effect.runSync(
      decodeError({
        schema_version: 2,
        control_protocol: FULL_CONTROL_V2,
        error: {
          code: 'conflict',
          message: 'another seat moved first',
          retryable: false,
          details: { safe_next: 'receipt_first', batch_id: 'batch_9' },
        },
        state_revision: { turn: 3, revision: 8, state_token: `token_${'0'.repeat(26)}` },
      })
    );
    // `--json` prints the validated envelope; the text path prints this.
    expect(renderErrorPayload(JSON.parse(JSON.stringify(payload)) as unknown)).toEqual([
      'error conflict: another seat moved first  rev8/t3',
      'next (receipt_first): resolve the outcome with `just receipt --batch_id batch_9` ' +
        'before any replay',
    ]);
  });

  test('RefusalRenderLive is the renderer, not the one-line default', () => {
    const lines = Effect.runSync(
      Effect.provide(
        Effect.map(RefusalRender, (renderer) =>
          renderer.renderErrorPayload({
            error: {
              code: 'rate_limited',
              message: 'slow down',
              retryable: true,
              details: { retry_after_seconds: 4 },
            },
          })
        ),
        Layer.mergeAll(RefusalRenderLive)
      )
    );
    expect(lines).toEqual([
      'error rate_limited: slow down',
      'next: retry the same command in 4s',
      RETRYABLE,
    ]);
  });

  test('the session identity is irrelevant here: rendering never re-validates', () => {
    // Guard against a future refactor that starts requiring a Session to print
    // a refusal — cli-main renders refusals from paths that have no session.
    expect(identity().gameId.length).toBeGreaterThan(0);
    expect(renderErrorPayload({ error: { code: 'x', message: 'y', details: {} } })).toEqual([
      'error x: y',
    ]);
  });
});
