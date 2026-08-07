/**
 * The refusal-rendering seam.
 *
 * `cli-main` has to render a `V2ResponseError`'s payload, and the renderer that
 * does it properly (`_render_error_payload`, client.py:5866-5910, with the
 * remedy table, the retry-after text and the cursor-restart command) belongs to
 * U14.  Core cannot import a file a unit owns and does not yet have, so the
 * dependency is inverted: cli-main asks for {@link RefusalRender}, and U14
 * supplies the real one by replacing {@link RefusalRenderDefault} in the Layer
 * stack.  Nothing else in cli-main changes when it lands.
 *
 * The default below is `_error_text` (client.py:5802-5804) and nothing more —
 * the one line that is always correct.
 */
import { Context, Layer } from 'effect';
import { isJsonObject } from 'src/schema/primitives';

export interface RefusalRenderApi {
  /** `_render_error_payload` — the refusal body, printed on stdout. */
  readonly renderErrorPayload: (payload: unknown) => ReadonlyArray<string>;
}

export class RefusalRender extends Context.Tag('RefusalRender')<
  RefusalRender,
  RefusalRenderApi
>() {}

/** `_error_text` — `"{code}: {message}"`. */
export const errorText = (payload: unknown): string => {
  if (!isJsonObject(payload)) return 'invalid_request: the supervisor refused this command';
  const body = payload['error'];
  if (!isJsonObject(body)) return 'invalid_request: the supervisor refused this command';
  const code = typeof body['code'] === 'string' ? body['code'] : 'invalid_request';
  const message = typeof body['message'] === 'string' ? body['message'] : '';
  return `${code}: ${message}`;
};

export const RefusalRenderDefault: Layer.Layer<RefusalRender> = Layer.succeed(RefusalRender, {
  renderErrorPayload: (payload) => [errorText(payload)],
});
