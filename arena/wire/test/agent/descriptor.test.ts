/**
 * Legal-action descriptors, checked against `_validate_descriptor`
 * (`play/client.py:1369-1401`).
 *
 * The two things worth pinning are the ones a caller's safety rests on: a
 * descriptor is only spendable at the revision it names, and its `kind` is a
 * pattern rather than an enum, so a supervisor may add capabilities without
 * breaking this build.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  ACTION_KIND_RE,
  decodeDescriptorAt,
  decodeLegalActionDescriptor,
  DESCRIPTOR_LABEL_MAX,
  descriptorLabelText,
  isActionKind,
  LegalActionDescriptor,
} from 'src/agent/descriptor';
import { decodeRevision } from 'src/agent/revision';
import { encodeTolerant } from 'src/tolerant';
import {
  descriptorWire,
  NEXT_REVISION,
  REPUBLISHED_REVISION,
  REVISION,
} from 'test/agent/wire-fixtures';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const revision = Either.getOrThrow(decodeRevision(REVISION));

describe('a well-formed descriptor', () => {
  test('decodes the shape test_client.py mints', () => {
    const decoded = Either.getOrThrow(decodeLegalActionDescriptor(descriptorWire()));
    expect(String(decoded.action_id)).toBe('action_opaque');
    expect(String(decoded.kind)).toBe('phase.end');
    expect(decoded.subject).toEqual({ operation: 'end' });
    expect(decoded.arguments_schema).toEqual({ type: 'object' });
  });

  test('a field a newer supervisor added survives a round trip', () => {
    const grown = descriptorWire({ hotkey: 'T' });
    const decoded = Either.getOrThrow(decodeLegalActionDescriptor(grown));
    const reencoded = Either.getOrThrow(encodeTolerant(LegalActionDescriptor)(decoded));
    expect(JSON.stringify(reencoded)).toBe(JSON.stringify(grown));
  });

  test('a missing field is still a refusal', () => {
    const { subject: _dropped, ...withoutSubject } = descriptorWire();
    expect(accepts(decodeLegalActionDescriptor(withoutSubject))).toBe(false);
  });
});

describe('kind is a pattern, not an enum', () => {
  test.each(['phase.end', 'unit.move', 'city.investigate', 'player.send_chat', 'a1.b2_c'])(
    'accepts %s, including kinds this build has never seen',
    (kind) => {
      expect(isActionKind(kind)).toBe(true);
      expect(accepts(decodeLegalActionDescriptor(descriptorWire({ kind })))).toBe(true);
    },
  );

  test.each(['phase', 'Phase.end', 'phase.End', 'phase.', '.end', 'phase..end', '1phase.end'])(
    'refuses %s',
    (kind) => {
      expect(ACTION_KIND_RE.test(kind)).toBe(false);
      expect(accepts(decodeLegalActionDescriptor(descriptorWire({ kind })))).toBe(false);
    },
  );

  test('the pattern is anchored, so a trailing newline is not a match', () => {
    expect(ACTION_KIND_RE.test('phase.end\n')).toBe(false);
  });
});

describe('label is bounded and non-blank', () => {
  test('refuses a label that is blank once trimmed', () => {
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ label: '   ' })))).toBe(false);
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ label: '' })))).toBe(false);
  });

  test('accepts exactly 240 characters and refuses 241', () => {
    const at = 'x'.repeat(DESCRIPTOR_LABEL_MAX);
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ label: at })))).toBe(true);
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ label: `${at}x` })))).toBe(false);
  });

  test('length is counted in code points, the way Python len() counts it', () => {
    const emoji = '🏛'.repeat(DESCRIPTOR_LABEL_MAX);
    expect(emoji.length).toBe(DESCRIPTOR_LABEL_MAX * 2);
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ label: emoji })))).toBe(true);
  });

  test('the label is kept verbatim; the trim is a separate reading', () => {
    const decoded = Either.getOrThrow(
      decodeLegalActionDescriptor(descriptorWire({ label: '  End phase  ' })),
    );
    expect(decoded.label).toBe('  End phase  ');
    expect(descriptorLabelText(decoded)).toBe('End phase');
  });
});

describe('subject and arguments_schema are objects, unshaped and bounded', () => {
  test('refuses a non-object subject', () => {
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ subject: 'end' })))).toBe(false);
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ subject: [] })))).toBe(false);
  });

  test('accepts any object shape — the Python never shaped them either', () => {
    const exotic = descriptorWire({
      subject: { operation: 'sabotage', building_choice: { id: 'b_1', name: 'Barracks' } },
      arguments_schema: { type: 'object', properties: {}, additionalProperties: false },
    });
    expect(accepts(decodeLegalActionDescriptor(exotic))).toBe(true);
  });

  test('refuses a subject beyond the _json_value key-count bound', () => {
    const wide = Object.fromEntries(
      Array.from({ length: 2049 }, (_unused, index) => [`k${String(index)}`, 1]),
    );
    expect(accepts(decodeLegalActionDescriptor(descriptorWire({ subject: wide })))).toBe(false);
  });
});

describe('a descriptor is only spendable at the revision it names', () => {
  test('accepts the revision it was minted at', () => {
    expect(accepts(decodeDescriptorAt(revision)(descriptorWire()))).toBe(true);
  });

  test('refuses a later revision', () => {
    expect(
      accepts(decodeDescriptorAt(revision)(descriptorWire({ state_revision: NEXT_REVISION }))),
    ).toBe(false);
  });

  test('refuses the same counters under a republished token', () => {
    expect(
      accepts(
        decodeDescriptorAt(revision)(descriptorWire({ state_revision: REPUBLISHED_REVISION })),
      ),
    ).toBe(false);
  });

  test('the unbound decoder does not check the revision at all', () => {
    expect(
      accepts(decodeLegalActionDescriptor(descriptorWire({ state_revision: NEXT_REVISION }))),
    ).toBe(true);
  });
});
