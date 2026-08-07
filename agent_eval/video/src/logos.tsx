/**
 * Harness and model-provider marks for the contender cards.
 *
 * A seat identifies itself with one string, `controller_label`, which is nothing
 * but `f"{harness}-{model}"` (`agent_eval/play_setup.py`). Splitting that back
 * apart on dashes is wrong the moment a model name starts with a harness name:
 * `claude-code-claude-opus-5` is the harness `claude-code` playing the model
 * `claude-opus-5`, and `split('-')[0]` calls it `claude`. So the split runs
 * against the known harness list, longest name first, and a label that matches
 * nothing gets nothing -- the Freeciv AI seats carry labels like
 * `Freeciv Classic AI` and must not be dressed up as contenders.
 *
 * Every mark is a local file under `public/logos/`, reached through
 * `staticFile()`: a render is headless and must never wait on a network fetch.
 *
 * Colour: the marks are painted, not shown. Each layer is a block of
 * `currentColor` cut to the SVG's alpha with a CSS mask, so the file's own fill
 * is irrelevant and the caller's `color` decides the ink. That is what makes one
 * asset work on the light page and on the dark board both -- the upstream files
 * disagree wildly about this (pi and opencode were drawn white for a dark ground,
 * Anthropic and Codex ship black, Claude ships brand orange), and masking settles
 * the argument by throwing all of their colours away.
 */

import type { CSSProperties, ReactElement } from 'react'
import { staticFile } from 'remotion'

/**
 * The harnesses that can hold a seat, from `HARNESS_CHOICES` in
 * `agent_eval/play_setup.py`. Adding one there means adding it here and dropping
 * a mark in `public/logos/harness/`.
 */
export const HARNESSES = ['claude-code', 'codex', 'opencode', 'pi'] as const

export type Harness = (typeof HARNESSES)[number]

/** The model vendors behind `MODEL_CHOICES` in `agent_eval/play_setup.py`. */
export const PROVIDERS = ['anthropic', 'google', 'openai'] as const

export type Provider = (typeof PROVIDERS)[number]

const HARNESS_NAMES: Record<Harness, string> = {
  'claude-code': 'Claude Code',
  codex: 'Codex',
  opencode: 'opencode',
  pi: 'pi',
}

const PROVIDER_NAMES: Record<Provider, string> = {
  anthropic: 'Anthropic',
  google: 'Google',
  openai: 'OpenAI',
}

export function isHarness(value: string): value is Harness {
  return (HARNESSES as readonly string[]).includes(value)
}

export function isProvider(value: string): value is Provider {
  return (PROVIDERS as readonly string[]).includes(value)
}

export function harnessName(harness: Harness): string {
  return HARNESS_NAMES[harness]
}

export function providerName(provider: Provider): string {
  return PROVIDER_NAMES[provider]
}

/**
 * Model-name prefixes, in the order they are tried. Prefixes rather than an
 * enumeration of model names: the roster turns over faster than this file does,
 * and a `gpt-6` nobody has added yet should still find its way to OpenAI.
 */
const PROVIDER_PREFIXES: readonly (readonly [string, Provider])[] = [
  ['gpt-', 'openai'],
  ['chatgpt', 'openai'],
  ['codex-', 'openai'],
  ['o1-', 'openai'],
  ['o3-', 'openai'],
  ['o4-', 'openai'],
  ['claude-', 'anthropic'],
  // Anthropic model families named without their `claude-` prefix, so
  // `claude-code-opus-5` is a legal label. Writing "claude" twice to say which
  // Claude model Claude Code ran is noise, and the harness already carries the
  // vendor -- but the model still has to resolve on its own, because that is
  // what decides the mark for any harness that is not Anthropic's.
  ['opus-', 'anthropic'],
  ['sonnet-', 'anthropic'],
  ['haiku-', 'anthropic'],
  ['fable-', 'anthropic'],
  ['gemini-', 'google'],
  ['gemma-', 'google'],
]

/**
 * The vendor a model name belongs to, or `null` for anything unrecognised --
 * which includes every non-agent seat, so callers can hand this a raw label
 * without checking first.
 */
export function providerForModel(model: string): Provider | null {
  const normalized = model.trim().toLowerCase()
  for (const [prefix, provider] of PROVIDER_PREFIXES) {
    if (normalized.startsWith(prefix)) return provider
  }
  return null
}

export interface ControllerParts {
  readonly harness: Harness | null
  readonly model: string | null
}

/**
 * Longest harness name first, so `claude-code` is offered before any shorter
 * name that could also open the label. Sorted once at module load rather than
 * hand-ordered, because a hand-ordered list is one careless insert away from
 * being wrong in a way no type catches.
 */
const HARNESSES_LONGEST_FIRST: readonly Harness[] = [...HARNESSES]
  .sort((left, right) => right.length - left.length)

/**
 * Pull the harness and model back out of a `controller_label`.
 *
 * A label with no known harness prefix still yields a model if the whole label
 * reads as one (a seat recorded bare, without its harness); otherwise both
 * halves come back `null`.
 */
export function splitControllerLabel(label: string): ControllerParts {
  const trimmed = label.trim()
  const normalized = trimmed.toLowerCase()
  for (const harness of HARNESSES_LONGEST_FIRST) {
    const prefix = `${harness}-`
    if (!normalized.startsWith(prefix)) continue
    const model = trimmed.slice(prefix.length).trim()
    return { harness, model: model.length > 0 ? model : null }
  }
  const bare = providerForModel(trimmed) === null ? null : trimmed
  return { harness: null, model: bare }
}

/**
 * The vendor prefix a harness lets its models leave off.
 *
 * A self-branded harness already says whose model it runs, so the label may
 * write `claude-code-opus-5` instead of `claude-code-claude-opus-5`. That is a
 * saving in the *label*, not in the name: on screen the model should still be
 * called what it is called everywhere else, so the prefix is restored for
 * display. The two directions are deliberate inverses -- shorten the key,
 * expand the name.
 */
const HARNESS_MODEL_PREFIX: Readonly<Partial<Record<Harness, string>>> = {
  'claude-code': 'claude-',
  codex: 'gpt-',
}

/**
 * What to call a model on screen: its recorded name, with the vendor prefix
 * its harness allowed it to omit put back.
 *
 * Only ever adds the prefix its own harness implies, so nothing is invented --
 * a model under `pi` is printed exactly as recorded, because there is no
 * harness vendor to borrow from.
 */
export function displayModelName(
  harness: Harness | null | undefined,
  model: string | null | undefined,
): string | null {
  const trimmed = model?.trim()
  if (!trimmed) return null
  const prefix = harness == null ? undefined : HARNESS_MODEL_PREFIX[harness]
  if (prefix === undefined) return trimmed
  return trimmed.toLowerCase().startsWith(prefix) ? trimmed : `${prefix}${trimmed}`
}

export interface ControllerMarks extends ControllerParts {
  readonly provider: Provider | null
}

/**
 * Everything a contender card needs from one label, nulls included. Takes the
 * nullable label straight off a `PlayerEntry` so the caller never has to guard.
 */
/**
 * Harnesses whose mark already says who made the model.
 *
 * Claude Code is Anthropic's and runs Claude; Codex is OpenAI's and runs GPT.
 * Showing the harness mark beside its own vendor's mark states the same fact
 * twice and reads as two competitors rather than one product. A harness from
 * neither vendor -- pi, opencode -- genuinely needs both, because the harness
 * and the model come from different places and which model ran is the point.
 *
 * Keyed on the harness, not on the pair: if Claude Code is ever pointed at a
 * GPT model the collapse would be wrong, but so would the assumption behind
 * the whole label, and a silent second mark is not how we would want to find
 * out.
 */
const SELF_BRANDED: ReadonlySet<Harness> = new Set<Harness>(['claude-code', 'codex'])

export function controllerMarks(label: string | null | undefined): ControllerMarks {
  if (label === null || label === undefined) {
    return { harness: null, model: null, provider: null }
  }
  const { harness, model } = splitControllerLabel(label)
  if (harness !== null && SELF_BRANDED.has(harness)) {
    return { harness, model, provider: null }
  }
  return { harness, model, provider: model === null ? null : providerForModel(model) }
}

/**
 * One painted pass of a mark. Most marks are a single opaque layer; opencode is
 * two, because its supplied form is genuinely two-tone and the inner block sits
 * flush against the frame -- painted at one strength they weld into a slab.
 * `opacity` is the tone, and it is relative to `currentColor`, so the pair holds
 * its relationship on a light page and on the dark board alike.
 */
interface MarkLayer {
  readonly file: string
  readonly opacity: number
}

const HARNESS_MARKS: Record<Harness, readonly MarkLayer[]> = {
  'claude-code': [{ file: 'logos/harness/claude-code.svg', opacity: 1 }],
  codex: [{ file: 'logos/harness/codex.svg', opacity: 1 }],
  opencode: [
    { file: 'logos/harness/opencode-frame.svg', opacity: 1 },
    { file: 'logos/harness/opencode-block.svg', opacity: 0.45 },
  ],
  pi: [{ file: 'logos/harness/pi.svg', opacity: 1 }],
}

const PROVIDER_MARKS: Record<Provider, readonly MarkLayer[]> = {
  anthropic: [{ file: 'logos/provider/anthropic.svg', opacity: 1 }],
  google: [{ file: 'logos/provider/google.svg', opacity: 1 }],
  openai: [{ file: 'logos/provider/openai.svg', opacity: 1 }],
}

/**
 * Every asset the registry can reach, as a `public/`-relative path. Exported so
 * a test can hold the files themselves to account: a mask fails *silently*, so
 * an SVG that does not parse or a download that turned out to be a 404 page
 * renders as a blank gap rather than an error anyone would notice.
 */
export const MARK_FILES: readonly string[] = [
  ...Object.values(HARNESS_MARKS).flat(),
  ...Object.values(PROVIDER_MARKS).flat(),
].map((layer) => layer.file)

const DEFAULT_SIZE = 24

/**
 * `| undefined` is spelled out on every optional: the package compiles with
 * `exactOptionalPropertyTypes`, under which `size?: number` refuses an explicit
 * `size={undefined}` and every one of these gets forwarded that way.
 */
export interface LogoProps {
  /** Side of the square the mark is fitted into, in px. */
  readonly size?: number | undefined
  readonly className?: string | undefined
  readonly style?: CSSProperties | undefined
  /** Accessible name; defaults to the mark's display name. */
  readonly title?: string | undefined
}

function maskStyle(layer: MarkLayer): CSSProperties {
  const url = `url(${staticFile(layer.file)})`
  return {
    position: 'absolute',
    inset: 0,
    backgroundColor: 'currentColor',
    opacity: layer.opacity,
    maskImage: url,
    WebkitMaskImage: url,
    maskSize: 'contain',
    WebkitMaskSize: 'contain',
    maskRepeat: 'no-repeat',
    WebkitMaskRepeat: 'no-repeat',
    maskPosition: 'center',
    WebkitMaskPosition: 'center',
  }
}

function Mark({
  layers, label, size, className, style,
}: {
  readonly layers: readonly MarkLayer[]
  readonly label: string
  readonly size: number
  readonly className?: string | undefined
  readonly style?: CSSProperties | undefined
}): ReactElement {
  return (
    <span
      role="img"
      aria-label={label}
      className={className}
      style={{
        position: 'relative',
        display: 'inline-block',
        flex: 'none',
        width: size,
        height: size,
        ...style,
      }}
    >
      {layers.map((layer) => (
        <span key={layer.file} style={maskStyle(layer)} />
      ))}
    </span>
  )
}

export interface HarnessLogoProps extends LogoProps {
  /** Raw or narrowed; anything unrecognised renders nothing. */
  readonly harness: string | null | undefined
}

/**
 * The harness's mark, or nothing at all. An agent nobody has taught this file
 * about draws no box and throws no error -- it just does not appear.
 */
export function HarnessLogo({
  harness, size = DEFAULT_SIZE, title, className, style,
}: HarnessLogoProps): ReactElement | null {
  if (harness === null || harness === undefined || !isHarness(harness)) return null
  return (
    <Mark
      layers={HARNESS_MARKS[harness]}
      label={title ?? HARNESS_NAMES[harness]}
      size={size}
      className={className}
      style={style}
    />
  )
}

export interface ProviderLogoProps extends LogoProps {
  /** Raw or narrowed; anything unrecognised renders nothing. */
  readonly provider: string | null | undefined
}

/** The model vendor's mark, on the same terms as {@link HarnessLogo}. */
export function ProviderLogo({
  provider, size = DEFAULT_SIZE, title, className, style,
}: ProviderLogoProps): ReactElement | null {
  if (provider === null || provider === undefined || !isProvider(provider)) return null
  return (
    <Mark
      layers={PROVIDER_MARKS[provider]}
      label={title ?? PROVIDER_NAMES[provider]}
      size={size}
      className={className}
      style={style}
    />
  )
}
