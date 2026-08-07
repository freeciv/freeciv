import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import {
  HARNESSES,
  MARK_FILES,
  PROVIDERS,
  controllerMarks,
  isHarness,
  isProvider,
  providerForModel,
  displayModelName,
  splitControllerLabel,
} from './logos'

/**
 * `controller_label` is `f"{harness}-{model}"` and nothing more
 * (`agent_eval/play_setup.py`), which makes it ambiguous the moment a model name
 * begins with a harness name. These cases pin the disambiguation, and the ones
 * marked as such are exactly the ones a positional split gets wrong.
 */

describe('splitControllerLabel', () => {
  it('splits the labels the exports actually carry', () => {
    expect(splitControllerLabel('pi-gpt-5.6-sol'))
      .toEqual({ harness: 'pi', model: 'gpt-5.6-sol' })
    expect(splitControllerLabel('pi-claude-opus-5'))
      .toEqual({ harness: 'pi', model: 'claude-opus-5' })
  })

  it('keeps claude-code whole instead of stopping at claude', () => {
    // The case a naive split('-') mangles: it would report the harness as
    // `claude` and the model as `code`.
    expect(splitControllerLabel('claude-code-claude-opus-5'))
      .toEqual({ harness: 'claude-code', model: 'claude-opus-5' })
    expect(splitControllerLabel('claude-code-claude-fable-5'))
      .toEqual({ harness: 'claude-code', model: 'claude-fable-5' })
    expect(splitControllerLabel('claude-code-gpt-5.5'))
      .toEqual({ harness: 'claude-code', model: 'gpt-5.5' })
  })

  it('does not let a shorter harness open a longer one', () => {
    // `codex` is a prefix of nothing here, but `opencode` would be shadowed by a
    // careless list order, and `pi` must not claim a model that merely rhymes.
    expect(splitControllerLabel('opencode-gemini-3-pro'))
      .toEqual({ harness: 'opencode', model: 'gemini-3-pro' })
    expect(splitControllerLabel('codex-gpt-5.6-sol'))
      .toEqual({ harness: 'codex', model: 'gpt-5.6-sol' })
  })

  it('covers every harness against every model in the roster', () => {
    const models = ['gpt-5.6-sol', 'gpt-5.5', 'claude-opus-5', 'claude-fable-5', 'gemini-3-pro']
    for (const harness of HARNESSES) {
      for (const model of models) {
        expect(splitControllerLabel(`${harness}-${model}`)).toEqual({ harness, model })
      }
    }
  })

  it('gives the non-agent seats nothing to wear', () => {
    // The Freeciv AI and the unclaimed places are not contenders and must not be
    // dressed as one.
    expect(splitControllerLabel('Freeciv Classic AI'))
      .toEqual({ harness: null, model: null })
    expect(splitControllerLabel('Freeciv dynamic faction'))
      .toEqual({ harness: null, model: null })
    expect(splitControllerLabel('Unclaimed agent place'))
      .toEqual({ harness: null, model: null })
    expect(splitControllerLabel('')).toEqual({ harness: null, model: null })
  })

  it('still names a model recorded without its harness', () => {
    expect(splitControllerLabel('claude-opus-5'))
      .toEqual({ harness: null, model: 'claude-opus-5' })
    expect(splitControllerLabel('gemini-3-pro'))
      .toEqual({ harness: null, model: 'gemini-3-pro' })
  })

  it('reports a harness with no model as a harness, not as a model', () => {
    expect(splitControllerLabel('pi-')).toEqual({ harness: 'pi', model: null })
    expect(splitControllerLabel('claude-code-')).toEqual({ harness: 'claude-code', model: null })
  })

  it('matches case-insensitively but hands back the recorded spelling', () => {
    expect(splitControllerLabel('  PI-GPT-5.6-Sol  '))
      .toEqual({ harness: 'pi', model: 'GPT-5.6-Sol' })
  })
})

describe('providerForModel', () => {
  it('places every model in the roster', () => {
    expect(providerForModel('gpt-5.6-sol')).toBe('openai')
    expect(providerForModel('gpt-5.5')).toBe('openai')
    expect(providerForModel('claude-opus-5')).toBe('anthropic')
    expect(providerForModel('claude-fable-5')).toBe('anthropic')
    expect(providerForModel('gemini-3-pro')).toBe('google')
  })

  it('places models nobody has added yet, since the roster turns over', () => {
    expect(providerForModel('o3-mini')).toBe('openai')
    expect(providerForModel('claude-sonnet-9')).toBe('anthropic')
    expect(providerForModel('gemma-4')).toBe('google')
  })

  it('is case- and whitespace-insensitive', () => {
    expect(providerForModel(' Claude-Opus-5 ')).toBe('anthropic')
  })

  it('returns null rather than guessing', () => {
    expect(providerForModel('Freeciv Classic AI')).toBeNull()
    expect(providerForModel('llama-4')).toBeNull()
    expect(providerForModel('')).toBeNull()
    // `claude-code` is a harness, not a model -- but it is also a legitimate
    // `claude-` prefix, so this documents that the split has to run first.
    expect(providerForModel('claude-code')).toBe('anthropic')
  })
})

describe('controllerMarks', () => {
  it('carries a label all the way to its marks', () => {
    // Claude Code moved to a single mark -- see the collapse cases below.
    expect(controllerMarks('claude-code-claude-opus-5'))
      .toEqual({ harness: 'claude-code', model: 'claude-opus-5', provider: null })
    expect(controllerMarks('pi-gpt-5.6-sol'))
      .toEqual({ harness: 'pi', model: 'gpt-5.6-sol', provider: 'openai' })
    expect(controllerMarks('opencode-gemini-3-pro'))
      .toEqual({ harness: 'opencode', model: 'gemini-3-pro', provider: 'google' })
  })

  it('takes a null label off a seat without a guard', () => {
    expect(controllerMarks(null)).toEqual({ harness: null, model: null, provider: null })
    expect(controllerMarks(undefined)).toEqual({ harness: null, model: null, provider: null })
  })

  it('keeps a known harness even when the model is unplaceable', () => {
    expect(controllerMarks('pi-llama-4'))
      .toEqual({ harness: 'pi', model: 'llama-4', provider: null })
  })
})

/**
 * A CSS mask that cannot load its image draws nothing and says nothing, so a
 * broken asset costs a mark in the film and no error anywhere. Both ways one
 * broke during this file's construction are checked here: a download that was
 * really a 404 HTML page, and a comment containing `--`, which XML forbids and
 * which makes the whole SVG unparseable.
 */
describe('the assets on disk', () => {
  const PUBLIC = join(dirname(fileURLToPath(import.meta.url)), '..', 'public')

  it.each(MARK_FILES)('%s is a loadable svg', (file) => {
    const source = readFileSync(join(PUBLIC, file), 'utf8')
    expect(source.toLowerCase()).not.toContain('<!doctype html')
    expect(source).toMatch(/<svg[\s>]/)
    expect(source).toContain('<path')
    for (const comment of source.match(/<!--[\s\S]*?-->/g) ?? []) {
      expect(comment.slice(4, -3)).not.toContain('--')
    }
  })

  it('paints every layer from currentColor, never a baked-in hex', () => {
    // The upstream files disagree about colour (white, black, brand orange); a
    // survivor would be invisible on one surface or the other.
    for (const file of MARK_FILES) {
      const source = readFileSync(join(PUBLIC, file), 'utf8')
      const drawn = source.replace(/<!--[\s\S]*?-->/g, '')
      expect(drawn).toContain('currentColor')
      expect(drawn).not.toMatch(/fill="#/)
    }
  })
})

describe('the guards', () => {
  it('accepts exactly the registered names', () => {
    for (const harness of HARNESSES) expect(isHarness(harness)).toBe(true)
    for (const provider of PROVIDERS) expect(isProvider(provider)).toBe(true)
    expect(isHarness('cursor')).toBe(false)
    expect(isHarness('Claude-Code')).toBe(false)
    expect(isProvider('meta')).toBe(false)
  })
})

describe('marks collapse when the harness is its own vendor', () => {
  it('shows only the harness for Claude Code and Codex', () => {
    // Both marks would say Anthropic twice, or OpenAI twice.
    expect(controllerMarks('claude-code-claude-opus-5'))
      .toEqual({ harness: 'claude-code', model: 'claude-opus-5', provider: null })
    expect(controllerMarks('codex-gpt-5.6-sol'))
      .toEqual({ harness: 'codex', model: 'gpt-5.6-sol', provider: null })
  })

  it('keeps both marks when the harness and the vendor differ', () => {
    // pi and opencode are neither Anthropic nor OpenAI, so which model ran is
    // a fact the harness mark cannot carry.
    expect(controllerMarks('pi-gpt-5.6-sol'))
      .toEqual({ harness: 'pi', model: 'gpt-5.6-sol', provider: 'openai' })
    expect(controllerMarks('pi-claude-opus-5'))
      .toEqual({ harness: 'pi', model: 'claude-opus-5', provider: 'anthropic' })
    expect(controllerMarks('opencode-gemini-3-pro'))
      .toEqual({ harness: 'opencode', model: 'gemini-3-pro', provider: 'google' })
  })
})

describe('a model may drop a vendor prefix its harness already carries', () => {
  it('reads claude-code-opus-5 the same as claude-code-claude-opus-5', () => {
    // Saying "claude" twice to name which Claude model Claude Code ran is
    // noise. Both spellings have to land on the same harness and the same
    // vendor, and the vendor still has to resolve from the model alone --
    // that is what decides the mark under a harness that is not Anthropic's.
    expect(splitControllerLabel('claude-code-opus-5'))
      .toEqual({ harness: 'claude-code', model: 'opus-5' })
    expect(controllerMarks('claude-code-opus-5'))
      .toEqual({ harness: 'claude-code', model: 'opus-5', provider: null })
    expect(controllerMarks('claude-code-claude-opus-5'))
      .toEqual({ harness: 'claude-code', model: 'claude-opus-5', provider: null })
  })

  it('places a bare Anthropic family name under a third-party harness', () => {
    expect(controllerMarks('pi-opus-5'))
      .toEqual({ harness: 'pi', model: 'opus-5', provider: 'anthropic' })
    expect(providerForModel('sonnet-5')).toBe('anthropic')
    expect(providerForModel('haiku-4-5')).toBe('anthropic')
    expect(providerForModel('fable-5')).toBe('anthropic')
  })
})

describe('codex short forms', () => {
  it('accepts a model named without its gpt- prefix', () => {
    // Same argument as claude-code-opus-5: Codex is OpenAI's and runs GPT, so
    // spelling gpt- again inside the label says nothing the harness has not
    // already said.
    expect(splitControllerLabel('codex-5.6-sol'))
      .toEqual({ harness: 'codex', model: '5.6-sol' })
    expect(splitControllerLabel('codex-sol'))
      .toEqual({ harness: 'codex', model: 'sol' })
  })

  it('gives all three spellings the same single mark', () => {
    for (const label of ['codex-gpt-5.6-sol', 'codex-5.6-sol', 'codex-sol']) {
      const { harness, provider } = controllerMarks(label)
      expect(harness).toBe('codex')
      expect(provider).toBeNull()
    }
  })
})

describe('displayModelName', () => {
  it('restores the prefix the harness let the label drop', () => {
    expect(displayModelName('claude-code', 'opus-5')).toBe('claude-opus-5')
    expect(displayModelName('codex', '5.6-sol')).toBe('gpt-5.6-sol')
  })

  it('leaves a model that already carries its prefix alone', () => {
    expect(displayModelName('claude-code', 'claude-opus-5')).toBe('claude-opus-5')
    expect(displayModelName('codex', 'gpt-5.6-sol')).toBe('gpt-5.6-sol')
  })

  it('invents nothing under a harness with no vendor of its own', () => {
    // pi and opencode borrow no prefix, so the recorded name is printed as-is.
    expect(displayModelName('pi', 'gpt-5.6-sol')).toBe('gpt-5.6-sol')
    expect(displayModelName('pi', 'opus-5')).toBe('opus-5')
    expect(displayModelName(null, 'opus-5')).toBe('opus-5')
    expect(displayModelName('claude-code', null)).toBeNull()
  })
})
