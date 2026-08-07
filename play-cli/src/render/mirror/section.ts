/**
 * The shape every mirror section renderer returns.
 *
 * `play/state_mirror.py`'s renderers are `(items, aliases) -> (columns, rows)`
 * tuples fed straight into `_table_text`.  TypeScript has no tuple destructuring
 * that survives a registry lookup, so the pair is named here once and imported
 * as a type by each renderer — the file carries no runtime code, so nothing in
 * `src/render/mirror/` depends on anything else in it.
 *
 * PORT_MAP amendment (U07, round 1): this file is not on U07's row in §0. It
 * exists because seven renderer files and the dispatch that calls them need one
 * name for the same pair; see NOTES §15.1.
 */
import type { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { MirrorAliases } from 'src/services/mirror';

/** One rendered mirror table: its column names and its rows, in page order. */
export interface RenderedSection {
  readonly columns: ReadonlyArray<string>;
  readonly rows: ReadonlyArray<ReadonlyArray<string>>;
}

/**
 * A section renderer.
 *
 * It fails rather than blanking a value: a payload item that is not an object
 * is a contract drift, and CPython raises `state mirror: … is not an object`
 * instead of writing a row of dashes over it.
 */
export type SectionRenderer = (
  items: ReadonlyArray<unknown>,
  aliases: MirrorAliases | null
) => Effect.Effect<RenderedSection, PlayerError>;
