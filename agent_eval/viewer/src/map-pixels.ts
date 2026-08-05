const NEAR_BLACK_LIMIT = 12

function clampChannel(value: number) {
  return Math.max(0, Math.min(255, Math.round(value)))
}

function colorKey(red: number, green: number, blue: number) {
  return (red << 16) | (green << 8) | blue
}

function parseHexColor(value: string) {
  const match = value.trim().match(/^#([0-9a-f]{6})$/i)
  if (!match) return null
  return Number.parseInt(match[1], 16)
}

function enhancedChannels(red: number, green: number, blue: number) {
  if (blue > red * 1.15 && blue > green * 1.08) {
    return [
      clampChannel(8 + red * .62 + green * .04),
      clampChannel(24 + green * .60 + blue * .05),
      clampChannel(38 + blue * .58),
    ]
  }

  if (green > red * 1.12 && green > blue * .98) {
    return [
      clampChannel(22 + red * .60 + green * .04),
      clampChannel(34 + green * .61),
      clampChannel(25 + blue * .57 + green * .02),
    ]
  }

  if (red > blue * 1.08 && green > blue * 1.08) {
    return [
      clampChannel(31 + red * .72),
      clampChannel(25 + green * .68),
      clampChannel(19 + blue * .62),
    ]
  }

  const luminance = red * .2126 + green * .7152 + blue * .0722
  return [
    clampChannel(luminance * .16 + red * .72 + 17),
    clampChannel(luminance * .18 + green * .70 + 15),
    clampChannel(luminance * .15 + blue * .70 + 13),
  ]
}

/**
 * Applies a presentation-only atlas treatment to RGBA pixels. The source
 * geometry is never changed, and exact faction colors are explicitly exempt.
 */
export function transformAtlasPixels(
  source: Uint8ClampedArray,
  protectedColors: readonly string[],
) {
  const result = new Uint8ClampedArray(source)
  const protectedKeys = new Set(
    protectedColors.flatMap((color) => {
      const parsed = parseHexColor(color)
      return parsed === null ? [] : [parsed]
    }),
  )

  for (let offset = 0; offset + 3 < result.length; offset += 4) {
    const red = result[offset]
    const green = result[offset + 1]
    const blue = result[offset + 2]

    if (protectedKeys.has(colorKey(red, green, blue))) continue

    if (
      red <= NEAR_BLACK_LIMIT
      && green <= NEAR_BLACK_LIMIT
      && blue <= NEAR_BLACK_LIMIT
    ) {
      result[offset + 3] = 0
      continue
    }

    const [nextRed, nextGreen, nextBlue] = enhancedChannels(red, green, blue)
    result[offset] = nextRed
    result[offset + 1] = nextGreen
    result[offset + 2] = nextBlue
  }

  return result
}
