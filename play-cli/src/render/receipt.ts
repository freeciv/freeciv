/**
 * Receipt and disposition rendering — one line per order, outcome first.
 *
 * Ports `_receipt_line` (client.py:5916-5928), `_observation_lines`
 * (5931-5940), `_render_receipt` (5943-5947) and `_render_disposition`
 * (5950-5964).
 *
 * The line shape is `"{intent} → {outcome} rev{r}/t{t}  {batch_id}"`, and every
 * part of it is load-bearing:
 *
 * - the **intent** is what the agent asked for, in the words it typed;
 * - the **outcome** leads with the receipt state, because that is the answer;
 * - `accepted` is the only non-terminal state, so it says so in the line
 *   itself — an agent that reads "accepted" as "done" has lost the game;
 * - the **batch ID goes last** so it can be copied off the end of the line into
 *   `just receipt --batch_id …` without selecting anything else.
 */
import { errorText } from 'src/render/refusal';
import { revisionLabel, signed } from 'src/render/primitives';
import type { BatchDisposition } from 'src/schema/batch';
import type { CityInvestigationObservation, CommandReceipt } from 'src/schema/receipt';

/**
 * The single receipt line.
 *
 * `accepted` carries its own warning because it is the one state that looks
 * final and is not: the server has taken the batch and has not yet resolved it.
 */
export const receiptLine = (receipt: CommandReceipt, intent: string): string => {
  const withError =
    receipt.error === null
      ? receipt.receipt_state
      : `${receipt.receipt_state} ${errorText(receipt.error)}`;
  const withIdempotent = receipt.idempotent ? `${withError} idempotent` : withError;
  const outcome = withIdempotent.startsWith('accepted')
    ? `${withIdempotent} (not final; resolve with just receipt)`
    : withIdempotent;
  return (
    `${intent} → ${outcome} ${revisionLabel(receipt.state_revision)}` +
    `  ${receipt.batch_id}`
  );
};

/** The one indented line a `city_investigation` observation is worth. */
export const observationLines = (
  observation: CityInvestigationObservation
): ReadonlyArray<string> => {
  const city = observation.city;
  return [
    `  investigated ${city.name} sz${city.size} ` +
      `${city.production.name} ` +
      `${city.shields.stock} shields ` +
      `(${signed(city.shields.surplus)}/turn), ` +
      `${city.improvements.length} improvements`,
  ];
};

export const renderReceipt = (
  receipt: CommandReceipt,
  intent: string
): ReadonlyArray<string> => [
  receiptLine(receipt, intent),
  ...(receipt.observation === null ? [] : observationLines(receipt.observation)),
];

/**
 * Render whatever the submit path came back with.
 *
 * A disposition without a receipt is the dangerous case: the batch may or may
 * not have applied, so the line names the disposition (`next=receipt_first`,
 * `next=retry_exact`, …) instead of implying an outcome it does not have.
 */
export const renderDisposition = (
  disposition: BatchDisposition,
  intent: string
): ReadonlyArray<string> => {
  if (disposition.receipt !== null) return renderReceipt(disposition.receipt, intent);
  const outcome =
    disposition.error === null
      ? 'outcome unknown'
      : `not accepted: ${errorText(disposition.error)}`;
  return [
    `${intent} → ${outcome} next=${disposition.disposition}` + `  ${disposition.batch_id}`,
  ];
};
