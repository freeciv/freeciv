import { Composition } from 'remotion'
import { DEFAULT_FILM_TIMING, GameFilm, filmDurationInFrames, type FilmProps } from './GameFilm'
import { loadMeta } from './dataset/load'

/**
 * One composition, parameterised by game id. `calculateMetadata` reads the
 * small `meta.json` for the requested game and sizes the timeline from its turn
 * count, so a render needs nothing but `--props '{"gameId":"..."}'`.
 */
export const DEFAULT_FILM_PROPS: FilmProps = {
  gameId: 'game_a8_dSs1WtX5NoDPHACckOKc4',
  framesPerTurn: DEFAULT_FILM_TIMING.framesPerTurn,
  titleFrames: DEFAULT_FILM_TIMING.titleFrames,
  outroFrames: DEFAULT_FILM_TIMING.outroFrames,
  superSample: 2,
  showCityLabels: true,
}

export function RemotionRoot() {
  return (
    <Composition
      calculateMetadata={async ({ props }) => {
        const meta = await loadMeta(props.gameId)
        return { durationInFrames: filmDurationInFrames(meta.frameCount, props) }
      }}
      component={GameFilm}
      defaultProps={DEFAULT_FILM_PROPS}
      durationInFrames={DEFAULT_FILM_TIMING.titleFrames + DEFAULT_FILM_TIMING.outroFrames}
      fps={30}
      height={1080}
      id="GameFilm"
      width={1920}
    />
  )
}
