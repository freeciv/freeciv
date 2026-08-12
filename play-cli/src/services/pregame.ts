/**
 * The pregame catalogs, the seat defaults and the phase-aware refusal.
 *
 * Ports `_phase_aware_refusal` (client.py:10802-10817),
 * `_mirror_pregame_catalog` (10820-10864), `_fetch_state_section` (10867-10889),
 * `_pregame_catalog` / `_pregame_choice` (10892-10927),
 * `_check_pregame_arguments` (10930-10942), `_sanitized_leader` (10952-10957),
 * `_pregame_default_nation` (10960-10981), `_pregame_seat_defaults`
 * (10984-11002) and `_cached_style_name` (11005-11010), plus the two constant
 * blocks at 10738-10752 and 10945-10949.
 *
 * `start` is the one command that runs before the game exists, so everything
 * here is about resolving a *name* the agent typed against a catalog the seat
 * already holds — and refusing, with the near misses spelled out, rather than
 * guessing which nation was meant.
 */
import { createHash } from 'node:crypto';
import { Effect } from 'effect';
import { OPAQUE_ID_RE, V2_LEGAL_DRAIN_MAX_PAGES } from 'src/constants';
import { PlayerError, playerError, type PlayError } from 'src/errors';
import { ambiguousChoiceRefusal, unknownChoiceRefusal } from 'src/render/pregame';
import type { BatchDisposition } from 'src/schema/batch';
import { decodePage, type PageEnvelope } from 'src/schema/index';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { aliasMap, rememberPage } from 'src/services/aliases';
import { CACHE_DIR, cachedPhaseNote, mirrorText, splitLines } from 'src/services/mirror';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, credentialsOf, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

// ---------------------------------------------------------------------------
// Constants (client.py:10738-10752, 10945-10949)
// ---------------------------------------------------------------------------

/** How one static pregame catalog names itself on the wire and in the mirror. */
export interface PregameCatalogSpec {
  /** The opaque-id prefix every row of this catalog must carry. */
  readonly prefix: string;
  /** The mirror projection this catalog was written to. */
  readonly mirror: ReadonlyArray<string>;
  /** The projection's second column, after `id`. */
  readonly column: string;
  /** What a refusal calls one row of it. */
  readonly label: string;
}

export const V2_PREGAME_CATALOGS: ReadonlyMap<string, PregameCatalogSpec> = new Map([
  [
    'pregame_nations',
    {
      prefix: 'nation_',
      mirror: [CACHE_DIR, 'nations.tsv'],
      column: 'nation',
      label: 'nation',
    },
  ],
  [
    'pregame_styles',
    {
      prefix: 'style_',
      mirror: [CACHE_DIR, 'styles.tsv'],
      column: 'style',
      label: 'style',
    },
  ],
]);

/** The longest leader name the boundary accepts, in UTF-8 bytes. */
export const V2_LEADER_MAX_BYTES = 47;

/**
 * Everything outside letters, digits, spaces, apostrophes, periods and hyphens
 * becomes a hyphen, so a harness label like `pi-gpt-5.5` survives as itself.
 */
export const LEADER_STRIP_RE = /[^0-9A-Za-z '.\-]+/g;

/** The characters CPython's `strip(" -.'")` takes off both ends. */
const LEADER_EDGE_RE = /^[ \-.']+|[ \-.']+$/g;

/** One row of a pregame catalog, as the resolver uses it. */
export interface PregameItem {
  readonly id: string;
  readonly name: string;
  readonly default_style_id?: string;
}

// ---------------------------------------------------------------------------
// _phase_aware_refusal
// ---------------------------------------------------------------------------

/**
 * Lead a refusal with the phase fact when this seat's turn is not live.
 *
 * Acting after the phase has ended looks exactly like a cold cache from inside
 * the resolver, and the cache-miss remedy then succeeds without fixing
 * anything — the agent re-enumerates, retries, and loops.  The mirror already
 * recorded whose phase it is, so the refusal says so first.
 *
 * CPython catches `PlayerError`, of which `V2ResponseError` is a subclass, and
 * re-raises a *plain* `PlayerError` carrying the joined text; the refusal body
 * that `cli-main` would have rendered from the payload is dropped in exchange
 * for the phase sentence.  The port keeps that trade rather than inventing a
 * third shape (NOTES.md §U18).
 *
 * `command_legal` (U11) and `command_batch` (U13) are its two callers.
 */
export const phaseAwareRefusal = <A, E extends PlayError, R>(
  sessionPath: string,
  body: Effect.Effect<A, E, R>
): Effect.Effect<A, E | PlayerError, R | PrivateFs> =>
  Effect.catchAll(body, (error): Effect.Effect<never, E | PlayerError, PrivateFs> =>
    Effect.flatMap(cachedPhaseNote(sessionPath), (note) =>
      note === '' || error.message.startsWith(note)
        ? Effect.fail<E | PlayerError>(error)
        : Effect.fail<E | PlayerError>(playerError(`${note}\n${error.message}`))
    )
  );

// ---------------------------------------------------------------------------
// _mirror_pregame_catalog
// ---------------------------------------------------------------------------

const cells = (line: string): ReadonlyArray<string> =>
  line.split('\t').map((cell) => cell.trim());

/**
 * Re-read a static catalog from the mirror this client itself wrote.
 *
 * A row is used only when it is provably intact: the projection must be marked
 * complete and every opaque ID must still be a full, untruncated ID.  Anything
 * else returns nothing, and the caller fetches the catalog again.
 */
export const mirrorPregameCatalog = (
  sessionPath: string,
  section: string
): Effect.Effect<ReadonlyArray<PregameItem>, never, PrivateFs> =>
  Effect.gen(function* () {
    const specification = V2_PREGAME_CATALOGS.get(section);
    if (specification === undefined) return [];
    const text = yield* mirrorText(sessionPath, specification.mirror);
    if (text === null) return [];
    const lines = splitLines(text);
    if (!lines.some((line) => line.startsWith('# ') && line.endsWith(' complete'))) {
      return [];
    }
    const body = lines.filter((line) => line !== '' && !line.startsWith('#'));
    const header = body[0];
    if (header === undefined || body.length < 2) return [];
    const columns = cells(header);
    if (columns[0] !== 'id' || columns[1] !== specification.column) return [];
    const items: PregameItem[] = [];
    for (const line of body.slice(1)) {
      const row = cells(line);
      if (row.length !== columns.length) return [];
      const named = new Map(columns.map((column, index) => [column, row[index] ?? '']));
      const identifier = named.get('id') ?? '';
      if (
        !identifier.startsWith(specification.prefix) ||
        !OPAQUE_ID_RE.test(identifier) ||
        identifier.length >= 64
      ) {
        return [];
      }
      const defaultStyle = named.get('default_style_id') ?? '';
      items.push({
        id: identifier,
        name: named.get(specification.column) ?? '',
        ...(defaultStyle.startsWith('style_') && defaultStyle.length < 64
          ? { default_style_id: defaultStyle }
          : {}),
      });
    }
    return items;
  });

// ---------------------------------------------------------------------------
// _fetch_state_section / _pregame_catalog
// ---------------------------------------------------------------------------

/** The ten-second budget `_fetch_state_section` gives one `/state` page. */
export const PREGAME_STATE_TIMEOUT_S = 10;

/** How many rows one `/state` drain asks for at a time. */
export const PREGAME_PAGE_LIMIT = 16;

/**
 * The seams `start` reaches across unit lines for.
 *
 * `mirrorPage` is U07's `update_from_page`; `resolveKindAction`/`drainLegal`
 * are U11/U12's catalog drain; `persistBatchForAction`/`submitPersistedBatch`
 * are U13's; `renderDisposition`/`receiptOk` are U14's.  They are injected
 * rather than imported so this unit compiles, and is testable end to end,
 * before any of them lands (NOTES.md §U18).
 */
export interface StartHooks {
  /** `_mirror_page` — project one validated page into the session's mirror. */
  readonly mirrorPage: (
    page: PageEnvelope,
    aliases: Readonly<Record<string, string>>,
    command: string
  ) => Effect.Effect<void>;
  /** `random.choice` over the sorted catalog. */
  readonly choose: (items: ReadonlyArray<PregameItem>) => PregameItem;
  /** `_resolve_kind_action` — one cached action of this kind, enumerating it if absent. */
  readonly resolveKindAction: (
    kind: string,
    remedy: string
  ) => Effect.Effect<PregameAction, PlayError>;
  /** `_drain_legal_unlocked` — re-enumerate this seat's capabilities in place. */
  readonly drainLegal: () => Effect.Effect<void, PlayError>;
  /** `_persist_batch_for_action` — write the batch before the POST; yields its id. */
  readonly persistBatchForAction: (
    actionId: string,
    argumentValues: JsonObject
  ) => Effect.Effect<string, PlayError>;
  /** `_submit_persisted_batch` — POST it and derive the disposition. */
  readonly submitPersistedBatch: (batchId: string) => Effect.Effect<SubmitOutcome, PlayError>;
  /** `_render_disposition`. */
  readonly renderDisposition: (
    disposition: BatchDisposition,
    intent: string
  ) => Effect.Effect<ReadonlyArray<string>, PlayError>;
  /** `_order_receipt_ok`. */
  readonly receiptOk: (disposition: BatchDisposition) => boolean;
}

/** What `_submit_persisted_batch` returns: the triple, named. */
export interface SubmitOutcome {
  readonly disposition: BatchDisposition;
  readonly warning: string | null;
  readonly exitCode: number;
}

/** The compact action shape `start` reads. U11's `CompactAction` satisfies it. */
export interface PregameAction {
  readonly action_id: string;
  readonly kind: string;
  readonly argument_schema: JsonValue;
}

/** Everything one `start` invocation resolves against. */
export interface PregameCtx {
  readonly sessionPath: string;
  readonly session: Session;
  readonly hooks: StartHooks;
}

/** Drain one state section into the cache and the mirror, printing none. */
export const fetchStateSection = (
  ctx: PregameCtx,
  section: string
): Effect.Effect<
  ReadonlyArray<JsonValue>,
  PlayError,
  V2Client | SessionStore | PrivateFs
> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(ctx.session);
    const base = yield* client.url(credentials, '/state');
    const items: JsonValue[] = [];
    let query = new URLSearchParams({
      section,
      limit: String(PREGAME_PAGE_LIMIT),
    }).toString();
    for (let page = 0; page < V2_LEGAL_DRAIN_MAX_PAGES; page += 1) {
      const response = yield* client.response('GET', `${base}?${query}`, credentials, {
        timeout: PREGAME_STATE_TIMEOUT_S,
      });
      if (response.status < 200 || response.status >= 300) {
        return yield* client.raiseValidated(response);
      }
      const value = yield* decodePage(response.value, ctx.session);
      // U03 takes the state lock, merges the page and saves; the mirror is then
      // written from the state that ingestion produced, exactly as CPython does.
      const remembered = yield* rememberPage(ctx.sessionPath, ctx.session, {
        legal: false,
        page: value,
      });
      const aliases = yield* aliasMap(remembered.state);
      yield* ctx.hooks.mirrorPage(value, aliases, 'start');
      items.push(...value.page.items);
      if (value.page.next_cursor === null) return items;
      query = new URLSearchParams({ cursor: value.page.next_cursor }).toString();
    }
    return yield* Effect.fail(
      playerError(`the ${section} catalog exceeded the safe drain limit`)
    );
  });

/**
 * Always a fresh drain — a cached read is never sufficient here.
 *
 * The server validates configure ids against the overlay recorded when THIS
 * seat read the section at the CURRENT native revision: the ids themselves
 * are MAC-stable, but an id read at an older revision has no overlay under
 * the validating revision and refuses as `pregame_*_unknown` (live finding,
 * game_Dn9l…: the lobby revision advances in the background, so the
 * join-time mirror is already stale by the first `start`; both seats
 * livelocked).  The mirror still receives every page as a side effect.
 */
export const pregameCatalog = (
  ctx: PregameCtx,
  section: string
): Effect.Effect<ReadonlyArray<PregameItem>, PlayError, V2Client | SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const items = yield* fetchStateSection(ctx, section);
    // CPython's resolvers key off `isinstance(item["name"], str)`: a row whose
    // name is not a string is invisible to both the match and the near-miss
    // list, so it never becomes a `PregameItem`.  A non-string id survives as
    // `''`, which is exactly as unmatchable and as undrawable.
    return items.filter(isJsonObject).flatMap((item): ReadonlyArray<PregameItem> => {
      if (typeof item['name'] !== 'string') return [];
      const style = item['default_style_id'];
      return [
        {
          id: typeof item['id'] === 'string' ? item['id'] : '',
          name: item['name'],
          ...(typeof style === 'string' && style !== '' ? { default_style_id: style } : {}),
        },
      ];
    });
  });

// ---------------------------------------------------------------------------
// _pregame_choice
// ---------------------------------------------------------------------------

/** Resolve one catalog entry by name, case-insensitively and exactly. */
export const pregameChoice = (
  items: ReadonlyArray<PregameItem>,
  wanted: string,
  label: string
): Effect.Effect<PregameItem, PlayerError> => {
  const folded = wanted.toLowerCase();
  const matches = items.filter((item) => item.name.toLowerCase() === folded && item.id !== '');
  const only = matches[0];
  if (matches.length === 1 && only !== undefined) return Effect.succeed(only);
  const known = items.map((item) => item.name).sort(compareStrings);
  const near = known.filter((name) => name.toLowerCase().includes(folded));
  return Effect.fail(
    matches.length > 1
      ? ambiguousChoiceRefusal(label, wanted)
      : unknownChoiceRefusal(label, wanted, near.length > 0 ? near : known)
  );
};

/** CPython's `sorted()` over strings: code-point order, stable. */
const compareStrings = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

// ---------------------------------------------------------------------------
// _check_pregame_arguments, plus the two U15 helpers it is written against
// ---------------------------------------------------------------------------

/**
 * `_order_properties` (client.py:8801-8813).
 *
 * On U15's row; ported here because `_check_pregame_arguments` is written
 * against it and U15 has not landed.  See the PORT_MAP amendment: when
 * `src/services/orders/arguments.ts` exists the integrator collapses the two.
 */
export const orderProperties = (
  action: PregameAction
): { readonly properties: JsonObject; readonly required: ReadonlyArray<string> } => {
  const schema = action.argument_schema;
  const properties = isJsonObject(schema) ? schema['properties'] : undefined;
  if (!isJsonObject(properties) || Object.keys(properties).length === 0) {
    return { properties: {}, required: [] };
  }
  const declared = isJsonObject(schema) ? schema['required'] : undefined;
  const required = (Array.isArray(declared) ? declared : []).filter(
    (name): name is string => typeof name === 'string' && name in properties
  );
  return { properties, required };
};

/**
 * `_default_arguments` (client.py:8946-8957) — fill an action whose required
 * arguments have exactly one legal value.  U15's row; see `orderProperties`.
 */
export const defaultArguments = (action: PregameAction): JsonObject | null => {
  const { properties, required } = orderProperties(action);
  const filled: Record<string, JsonValue> = {};
  for (const name of required) {
    const specification = properties[name];
    const choices = isJsonObject(specification) ? specification['enum'] : undefined;
    if (!Array.isArray(choices) || choices.length !== 1) return null;
    filled[name] = choices[0] as JsonValue;
  }
  return filled;
};

/**
 * `_order_receipt_ok` (client.py:9483-9488) — U14's row.
 *
 * U18 landed a local copy because `src/services/disposition.ts` did not exist
 * yet; per PORT_MAP's amendment the integrator collapses it to a re-export once
 * the owning unit lands, so there is exactly one definition of "did this order
 * actually go through".
 */
export { orderReceiptOk } from 'src/services/disposition';

/** Refuse when the enumerated action does not take the arguments we built. */
export const checkPregameArguments = (
  action: PregameAction,
  argumentValues: JsonObject
): Effect.Effect<void, PlayerError> => {
  const { properties, required } = orderProperties(action);
  const missing = required.filter((name) => !(name in argumentValues));
  const unknown = Object.keys(argumentValues).filter((name) => !(name in properties));
  return missing.length > 0 || unknown.length > 0
    ? Effect.fail(
        playerError(
          `the enumerated ${action.kind} action does not take the arguments ` +
            'this workspace builds; run ' +
            `\`just legal --kind ${action.kind} --all --json\` and submit it with \`just batch\``
        )
      )
    : Effect.void;
};

// ---------------------------------------------------------------------------
// _sanitized_leader
// ---------------------------------------------------------------------------

const utf8Length = (text: string): number => new TextEncoder().encode(text).length;

/** Reduce a controller label to a leader name Freeciv will accept. */
export const sanitizedLeader = (label: string): string => {
  const collapsed = label.replace(LEADER_STRIP_RE, '-').replace(/\s+/gu, ' ');
  let text = collapsed.replace(LEADER_EDGE_RE, '');
  // CPython slices code points; every survivor of the strip above is ASCII, so
  // the two agree — `Array.from` keeps them agreeing if that ever changes.
  while (utf8Length(text) > V2_LEADER_MAX_BYTES) {
    const points = Array.from(text);
    points.pop();
    text = points.join('');
  }
  return text.replace(LEADER_EDGE_RE, '');
};

/**
 * The sex a seat gets when neither the flags nor the lobby named one.
 *
 * A pure function of the resolved leader name, so two runs of the same
 * zero-argument command configure the same seat.
 */
export const defaultSex = (leader: string): 'male' | 'female' => {
  const digest = createHash('sha256').update(leader, 'utf8').digest();
  return (digest[0] ?? 0) % 2 !== 0 ? 'male' : 'female';
};

// ---------------------------------------------------------------------------
// _pregame_default_nation
// ---------------------------------------------------------------------------

/**
 * Pick one nation at random from what the lobby actually offers.
 *
 * Nations are cosmetic under this eval's fixed-trait setup, so `just start`
 * with no arguments picks one rather than making the agent read a 50-row
 * catalog to type a name back.  The pick is made over the *sorted* catalog, so
 * seeding the RNG makes it reproducible.
 */
export const pregameDefaultNation = (
  items: ReadonlyArray<PregameItem>,
  choose: (choices: ReadonlyArray<PregameItem>) => PregameItem
): Effect.Effect<PregameItem, PlayerError> => {
  const choices = items
    .filter((item) => item.id !== '' && item.name !== '')
    .map((item, index) => ({ item, index }))
    .sort((left, right) => compareStrings(left.item.name, right.item.name) || left.index - right.index)
    .map((entry) => entry.item);
  return choices.length === 0
    ? Effect.fail(
        playerError(
          'this lobby offers no selectable nation; read the catalog with ' +
            '`just state --section pregame_nations`'
        )
      )
    : Effect.succeed(choose(choices));
};

// ---------------------------------------------------------------------------
// _pregame_seat_defaults, _cached_style_name
// ---------------------------------------------------------------------------

/** What the lobby already holds for this seat, when the flags said nothing. */
export interface SeatDefaults {
  readonly leader: string;
  readonly sex: string;
}

/**
 * Read the leader and sex the lobby already holds for this seat.
 *
 * The lobby `overview` carries the seat's own pregame identity, so a missing
 * `--leader`/`--male|--female` resolves against the catalog rather than against
 * a value this client invented.
 */
export const pregameSeatDefaults = (
  ctx: PregameCtx
): Effect.Effect<SeatDefaults, PlayError, V2Client | SessionStore | PrivateFs> =>
  Effect.map(fetchStateSection(ctx, 'overview'), (items) => {
    const first = items[0];
    const player = isJsonObject(first) ? first['player'] : undefined;
    if (!isJsonObject(player)) return { leader: '', sex: '' };
    const leader = player['leader_name'];
    const sex = player['sex'];
    return {
      leader: typeof leader === 'string' ? leader : '',
      sex: sex === 'male' || sex === 'female' ? sex : '',
    };
  });

/** Name a style from the mirror only; never spend a request on a label. */
export const cachedStyleName = (
  sessionPath: string,
  styleId: string
): Effect.Effect<string, never, PrivateFs> =>
  Effect.map(
    mirrorPregameCatalog(sessionPath, 'pregame_styles'),
    (items) => items.find((item) => item.id === styleId)?.name ?? ''
  );
