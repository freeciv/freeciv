import { useEffect, useRef, useState, type MutableRefObject } from 'react'
import * as THREE from 'three'
import { MapControls } from 'three/addons/controls/MapControls.js'
import {
  buildBoardTiles,
  isoHexNeighborCoordinates,
  isWaterTerrain,
  nativeTilePosition,
  ownershipBoundaryEdges,
  terrainColor,
  terrainLift,
} from '../board-geometry'
import { factionDisplayLabel } from '../faction-label'
import type { BoardResponse } from '../types'

export type BoardViewMode = 'political' | 'terrain'

export interface BoardActions {
  fit: () => void
  zoomIn: () => void
  zoomOut: () => void
}

interface ThreeBoardProps {
  actionsRef: MutableRefObject<BoardActions | null>
  alt: string
  board: BoardResponse
  mode: BoardViewMode
  onCommit: (board: BoardResponse) => void
  onFailure: () => void
}

interface BoardLayer {
  group: THREE.Group
  shapeSignature: string
  topByTile: number[]
}

interface ThreeRuntime {
  camera: THREE.OrthographicCamera
  controls: MapControls
  current: BoardLayer | null
  fit: () => void
  render: () => void
  renderer: THREE.WebGLRenderer
  scene: THREE.Scene
}

function ownerColor(board: BoardResponse, playerId: number | null): string | null {
  if (playerId === null) return null
  return board.players.find((player) => player.player_id === playerId)?.player_color ?? null
}

function addInstances(
  group: THREE.Group,
  geometry: THREE.BufferGeometry,
  material: THREE.Material,
  matrices: THREE.Matrix4[],
): THREE.InstancedMesh | null {
  if (!matrices.length) {
    geometry.dispose()
    material.dispose()
    return null
  }
  const mesh = new THREE.InstancedMesh(geometry, material, matrices.length)
  matrices.forEach((matrix, index) => mesh.setMatrixAt(index, matrix))
  mesh.instanceMatrix.needsUpdate = true
  mesh.frustumCulled = false
  group.add(mesh)
  return mesh
}

function instanceMatrix(
  x: number,
  y: number,
  z: number,
  scaleX = 1,
  scaleY = 1,
  scaleZ = 1,
  rotationY = 0,
): THREE.Matrix4 {
  return new THREE.Matrix4().compose(
    new THREE.Vector3(x, y, z),
    new THREE.Quaternion().setFromEuler(new THREE.Euler(0, rotationY, 0)),
    new THREE.Vector3(scaleX, scaleY, scaleZ),
  )
}

function labelSprite(text: string, color: string): THREE.Sprite {
  const canvas = document.createElement('canvas')
  canvas.width = 768
  canvas.height = 128
  const context = canvas.getContext('2d')
  if (!context) throw new Error('Canvas labels are unavailable')
  context.fillStyle = 'rgba(10, 13, 14, .9)'
  context.strokeStyle = color
  context.lineWidth = 7
  context.beginPath()
  context.roundRect(5, 5, canvas.width - 10, canvas.height - 10, 20)
  context.fill()
  context.stroke()
  context.fillStyle = '#fff8e7'
  context.font = '700 42px ui-monospace, SFMono-Regular, Menlo, monospace'
  context.textAlign = 'center'
  context.textBaseline = 'middle'
  const fitted = text.length > 28 ? `${text.slice(0, 27)}…` : text
  context.fillText(fitted, canvas.width / 2, canvas.height / 2 + 1)
  const texture = new THREE.CanvasTexture(canvas)
  texture.colorSpace = THREE.SRGBColorSpace
  texture.minFilter = THREE.LinearFilter
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({
    depthTest: false,
    depthWrite: false,
    map: texture,
    transparent: true,
  }))
  sprite.scale.set(15, 2.5, 1)
  sprite.renderOrder = 50
  return sprite
}

function buildBoardLayer(board: BoardResponse, mode: BoardViewMode): BoardLayer {
  const tiles = buildBoardTiles(board)
  const group = new THREE.Group()
  group.name = `semantic-board-turn-${board.turn}`
  let minAltitude = Number.POSITIVE_INFINITY
  let maxAltitude = Number.NEGATIVE_INFINITY
  for (const tile of tiles) {
    minAltitude = Math.min(minAltitude, tile.altitude)
    maxAltitude = Math.max(maxAltitude, tile.altitude)
  }
  const altitudeSpan = Math.max(1, maxAltitude - minAltitude)
  const topByTile = Array.from({ length: tiles.length }, () => 0)
  const terrainGroups = new Map<string, THREE.Matrix4[]>()

  for (const tile of tiles) {
    const altitude = (tile.altitude - minAltitude) / altitudeSpan
    const height = isWaterTerrain(tile.terrainName)
      ? 0.08
      : 0.14 + terrainLift(tile.terrainName) + altitude * 0.24
    topByTile[tile.index] = height
    const matrices = terrainGroups.get(tile.terrainName) ?? []
    matrices.push(instanceMatrix(
      tile.worldX, height / 2, tile.worldZ, 1, height, 1, Math.PI / 6,
    ))
    terrainGroups.set(tile.terrainName, matrices)
  }

  for (const [name, matrices] of terrainGroups) {
    addInstances(
      group,
      new THREE.CylinderGeometry(0.585, 0.585, 1, 6, 1, false),
      new THREE.MeshStandardMaterial({
        color: terrainColor(name),
        flatShading: true,
        metalness: 0,
        roughness: 0.94,
      }),
      matrices,
    )
  }

  const territoryByPlayer = new Map<number, THREE.Matrix4[]>()
  const unownedMatrices: THREE.Matrix4[] = []
  for (const tile of tiles) {
    if (tile.ownerId === null || !ownerColor(board, tile.ownerId)) {
      if (mode === 'political') {
        unownedMatrices.push(instanceMatrix(
          tile.worldX, topByTile[tile.index] + 0.014, tile.worldZ,
          1, 1, 1, Math.PI / 6,
        ))
      }
      continue
    }
    const matrices = territoryByPlayer.get(tile.ownerId) ?? []
    matrices.push(instanceMatrix(
      tile.worldX, topByTile[tile.index] + 0.014, tile.worldZ,
      1, 1, 1, Math.PI / 6,
    ))
    territoryByPlayer.set(tile.ownerId, matrices)
  }
  for (const [playerId, matrices] of territoryByPlayer) {
    const color = ownerColor(board, playerId) ?? '#ffffff'
    const fill = new THREE.CircleGeometry(mode === 'political' ? 0.565 : 0.515, 6)
    fill.rotateX(-Math.PI / 2)
    addInstances(group, fill, new THREE.MeshBasicMaterial({
      color,
      depthWrite: false,
      opacity: mode === 'political' ? 0.78 : 0.2,
      side: THREE.DoubleSide,
      transparent: true,
    }), matrices)
  }
  if (mode === 'political') {
    const unownedFill = new THREE.CircleGeometry(0.515, 6)
    unownedFill.rotateX(-Math.PI / 2)
    addInstances(group, unownedFill, new THREE.MeshBasicMaterial({
      color: '#445156',
      depthWrite: false,
      opacity: 0.34,
      side: THREE.DoubleSide,
      transparent: true,
    }), unownedMatrices)
  }

  const boundaryByColor = new Map<string, THREE.Matrix4[]>()
  for (const edge of ownershipBoundaryEdges(board)) {
    const tile = tiles[edge.tileIndex]
    const neighborNative = isoHexNeighborCoordinates(edge.x, edge.y)[edge.direction]
    if (!tile || !neighborNative) continue
    const neighbor = nativeTilePosition(neighborNative.x, neighborNative.z, board.width)
    const deltaX = neighbor.x - tile.worldX
    const deltaZ = neighbor.z - tile.worldZ
    const distance = Math.hypot(deltaX, deltaZ) || 1
    const normalX = deltaX / distance
    const normalZ = deltaZ / distance
    const color = edge.neighborOwnerId === null
      ? ownerColor(board, edge.ownerId) ?? '#f4e7c5'
      : '#fff1c9'
    const matrices = boundaryByColor.get(color) ?? []
    matrices.push(instanceMatrix(
      tile.worldX + normalX * 0.545,
      topByTile[tile.index] + 0.05,
      tile.worldZ + normalZ * 0.545,
      1, 1, 1,
      Math.atan2(normalZ, normalX) + Math.PI / 2,
    ))
    boundaryByColor.set(color, matrices)
  }
  for (const [color, matrices] of boundaryByColor) {
    addInstances(
      group,
      new THREE.BoxGeometry(0.64, 0.035, mode === 'political' ? 0.075 : 0.045),
      new THREE.MeshBasicMaterial({ color, depthWrite: false, opacity: mode === 'political' ? 0.98 : 0.78, transparent: true }),
      matrices,
    )
  }

  const extraNames = new Map(board.extras_catalog.map((extra) => [extra.id, extra.name]))
  const roadMatrices: THREE.Matrix4[] = []
  const railMatrices: THREE.Matrix4[] = []
  const riverMatrices: THREE.Matrix4[] = []
  const resourceMatrices: THREE.Matrix4[] = []
  const infrastructure = /irrigation|mine|oil well|pollution|hut|farmland|fallout|fortress|airbase|buoy|ruins|road|railroad|river/i
  for (const tile of tiles) {
    const names = tile.extraIds.map((id) => extraNames.get(id) ?? '')
    const rotation = ((tile.x + tile.y) % 3) * Math.PI / 3
    const y = topByTile[tile.index] + 0.035
    if (names.includes('Road')) roadMatrices.push(instanceMatrix(tile.worldX, y, tile.worldZ, 1, 1, 1, rotation))
    if (names.includes('Railroad')) railMatrices.push(instanceMatrix(tile.worldX, y + 0.012, tile.worldZ, 1, 1, 1, rotation))
    if (names.includes('River')) riverMatrices.push(instanceMatrix(tile.worldX, y + 0.018, tile.worldZ, 1, 1, 1, rotation + Math.PI / 3))
    if (names.some((name) => name && !infrastructure.test(name))) {
      resourceMatrices.push(instanceMatrix(tile.worldX + 0.2, y + 0.1, tile.worldZ - 0.16))
    }
  }
  addInstances(group, new THREE.BoxGeometry(0.72, 0.025, 0.055), new THREE.MeshStandardMaterial({ color: '#b49b70', roughness: 0.8 }), roadMatrices)
  addInstances(group, new THREE.BoxGeometry(0.76, 0.032, 0.075), new THREE.MeshStandardMaterial({ color: '#53504a', metalness: 0.18, roughness: 0.68 }), railMatrices)
  addInstances(group, new THREE.BoxGeometry(0.7, 0.025, 0.075), new THREE.MeshStandardMaterial({ color: '#59a5bb', roughness: 0.55 }), riverMatrices)
  addInstances(group, new THREE.IcosahedronGeometry(0.085, 0), new THREE.MeshStandardMaterial({ color: '#d7bd70', emissive: '#4a3211', roughness: 0.52 }), resourceMatrices)

  const citiesByPlayer = new Map<number, THREE.Matrix4[]>()
  const cityCenters: THREE.Matrix4[] = []
  const capitalsByPlayer = new Map<number, THREE.Matrix4[]>()
  const labelCityByPlayer = new Map<number, (typeof board.cities)[number]>()
  for (const city of board.cities) {
    const position = nativeTilePosition(city.x, city.y, board.width)
    const top = topByTile[city.y * board.width + city.x] ?? 0.1
    const scale = 0.9 + Math.min(24, city.size) * 0.025
    const matrix = instanceMatrix(position.x - 0.12, top + 0.25, position.z + 0.1, scale, scale, scale)
    const matrices = citiesByPlayer.get(city.player_id) ?? []
    matrices.push(matrix)
    citiesByPlayer.set(city.player_id, matrices)
    cityCenters.push(instanceMatrix(position.x - 0.12, top + 0.5, position.z + 0.1, scale, scale, scale))
    const priorLabelCity = labelCityByPlayer.get(city.player_id)
    if (!priorLabelCity || city.capital || (!priorLabelCity.capital && city.size > priorLabelCity.size)) {
      labelCityByPlayer.set(city.player_id, city)
    }
    if (city.capital) {
      const capitals = capitalsByPlayer.get(city.player_id) ?? []
      capitals.push(instanceMatrix(position.x - 0.12, top + 0.48, position.z + 0.1, scale, scale, scale))
      capitalsByPlayer.set(city.player_id, capitals)
    }
  }
  for (const [playerId, matrices] of citiesByPlayer) {
    addInstances(
      group,
      new THREE.CylinderGeometry(0.22, 0.34, 0.48, 8),
      new THREE.MeshStandardMaterial({ color: ownerColor(board, playerId) ?? '#e7dfce', emissive: '#231c12', emissiveIntensity: 0.32, roughness: 0.46 }),
      matrices,
    )
  }
  addInstances(
    group,
    new THREE.CylinderGeometry(0.115, 0.115, 0.07, 12),
    new THREE.MeshBasicMaterial({ color: '#fff8df' }),
    cityCenters,
  )
  for (const [, matrices] of capitalsByPlayer) {
    const capitalRing = new THREE.TorusGeometry(0.28, 0.048, 6, 18)
    capitalRing.rotateX(Math.PI / 2)
    addInstances(group, capitalRing, new THREE.MeshBasicMaterial({ color: '#fff0a2' }), matrices)
  }

  if (mode === 'political') {
    for (const [playerId, city] of labelCityByPlayer) {
      const player = board.players.find((candidate) => candidate.player_id === playerId)
      if (!player) continue
      const position = nativeTilePosition(city.x, city.y, board.width)
      const top = topByTile[city.y * board.width + city.x] ?? 0.1
      const sprite = labelSprite(
        factionDisplayLabel(player),
        ownerColor(board, playerId) ?? '#fff1c9',
      )
      sprite.position.set(position.x, top + 1.15, position.z)
      group.add(sprite)
    }
  }

  const unitsByPlayer = new Map<number, THREE.Matrix4[]>()
  for (const stack of board.unit_stacks) {
    const position = nativeTilePosition(stack.x, stack.y, board.width)
    const top = topByTile[stack.y * board.width + stack.x] ?? 0.1
    const scale = 0.75 + Math.min(0.75, Math.log2(stack.count + 1) * 0.13)
    const matrices = unitsByPlayer.get(stack.player_id) ?? []
    matrices.push(instanceMatrix(position.x + 0.17, top + 0.19, position.z - 0.13, scale, scale, scale, Math.PI / 4))
    unitsByPlayer.set(stack.player_id, matrices)
  }
  for (const [playerId, matrices] of unitsByPlayer) {
    addInstances(
      group,
      new THREE.OctahedronGeometry(0.11, 0),
      new THREE.MeshStandardMaterial({ color: ownerColor(board, playerId) ?? '#f1ede4', emissive: '#15120f', metalness: 0.12, roughness: 0.38 }),
      matrices,
    )
  }

  return {
    group,
    shapeSignature: `${board.width}x${board.height}:${board.topology}:${board.wrap}`,
    topByTile,
  }
}

function disposeLayer(layer: BoardLayer | null): void {
  if (!layer) return
  layer.group.traverse((object) => {
    if (!(object instanceof THREE.Mesh) && !(object instanceof THREE.Sprite)) return
    if (object instanceof THREE.Mesh) object.geometry.dispose()
    const materials = Array.isArray(object.material) ? object.material : [object.material]
    materials.forEach((material) => {
      const map = (material as THREE.SpriteMaterial).map
      if (map) map.dispose()
      material.dispose()
    })
  })
}

export function ThreeBoard({ actionsRef, alt, board, mode, onCommit, onFailure }: ThreeBoardProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const runtimeRef = useRef<ThreeRuntime | null>(null)
  const sceneGeneration = useRef(0)
  const [ready, setReady] = useState(false)

  useEffect(() => {
    const container = containerRef.current
    if (!container) return
    let renderer: THREE.WebGLRenderer
    try {
      renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true, powerPreference: 'high-performance' })
    } catch {
      onFailure()
      return
    }
    renderer.outputColorSpace = THREE.SRGBColorSpace
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1))
    renderer.shadowMap.enabled = false
    renderer.domElement.className = 'three-board-canvas'
    renderer.domElement.setAttribute('aria-hidden', 'true')
    container.append(renderer.domElement)

    const scene = new THREE.Scene()
    scene.background = new THREE.Color('#102629')
    scene.fog = new THREE.FogExp2('#102629', 0.0045)
    const camera = new THREE.OrthographicCamera(-10, 10, 10, -10, 0.1, 1000)
    const controls = new MapControls(camera, renderer.domElement)
    controls.enableRotate = false
    controls.enableDamping = false
    controls.screenSpacePanning = true
    controls.minZoom = 0.35
    controls.maxZoom = 8
    scene.add(new THREE.HemisphereLight('#e7f2ed', '#344032', 2.7))
    const key = new THREE.DirectionalLight('#fff2d4', 3.15)
    key.position.set(-15, 35, 18)
    scene.add(key)

    const render = () => renderer.render(scene, camera)
    const fit = () => {
      const current = runtimeRef.current?.current
      const width = Math.max(1, container.clientWidth)
      const height = Math.max(1, container.clientHeight)
      const aspect = width / height
      const box = current ? new THREE.Box3().setFromObject(current.group) : new THREE.Box3(
        new THREE.Vector3(-5, 0, -5), new THREE.Vector3(5, 1, 5),
      )
      const size = box.getSize(new THREE.Vector3())
      const center = box.getCenter(new THREE.Vector3())
      const span = Math.max(size.z + size.y * 1.2, size.x / aspect, 8) * 1.12
      camera.left = -span * aspect / 2
      camera.right = span * aspect / 2
      camera.top = span / 2
      camera.bottom = -span / 2
      camera.position.set(center.x, Math.max(18, span * 1.25), center.z + span * 0.72)
      camera.zoom = 1.32
      camera.lookAt(center.x, 0, center.z)
      camera.updateProjectionMatrix()
      controls.target.set(center.x, 0, center.z)
      controls.update()
      render()
    }
    const resize = () => {
      const width = Math.max(1, container.clientWidth)
      const height = Math.max(1, container.clientHeight)
      renderer.setSize(width, height, false)
      fit()
    }
    const runtime: ThreeRuntime = { camera, controls, current: null, fit, render, renderer, scene }
    runtimeRef.current = runtime
    actionsRef.current = {
      fit,
      zoomIn: () => { camera.zoom = Math.min(controls.maxZoom, camera.zoom * 1.25); camera.updateProjectionMatrix(); render() },
      zoomOut: () => { camera.zoom = Math.max(controls.minZoom, camera.zoom / 1.25); camera.updateProjectionMatrix(); render() },
    }
    controls.addEventListener('change', render)
    const observer = new ResizeObserver(resize)
    observer.observe(container)
    resize()
    setReady(true)
    return () => {
      setReady(false)
      observer.disconnect()
      controls.removeEventListener('change', render)
      controls.dispose()
      disposeLayer(runtime.current)
      renderer.dispose()
      renderer.forceContextLoss()
      renderer.domElement.remove()
      actionsRef.current = null
      runtimeRef.current = null
    }
  }, [actionsRef, onFailure])

  useEffect(() => {
    const runtime = runtimeRef.current
    if (!ready || !runtime) return
    const generation = ++sceneGeneration.current
    let frame = 0
    let buildTimer = 0
    let built: BoardLayer | null = null
    frame = requestAnimationFrame(() => {
      buildTimer = window.setTimeout(() => {
        try {
          built = buildBoardLayer(board, mode)
        } catch {
          onFailure()
          return
        }
        if (generation !== sceneGeneration.current) {
          disposeLayer(built)
          return
        }
        frame = requestAnimationFrame(() => {
          if (!built || generation !== sceneGeneration.current) {
            disposeLayer(built)
            return
          }
          const previous = runtime.current
          runtime.scene.add(built.group)
          runtime.current = built
          if (previous) runtime.scene.remove(previous.group)
          if (!previous || previous.shapeSignature !== built.shapeSignature) runtime.fit()
          else runtime.render()
          if (previous) disposeLayer(previous)
          onCommit(board)
        })
      }, 0)
    })
    return () => {
      sceneGeneration.current += 1
      if (frame) cancelAnimationFrame(frame)
      if (buildTimer) window.clearTimeout(buildTimer)
      if (built && runtime.current !== built) disposeLayer(built)
    }
  }, [board, mode, onCommit, onFailure, ready])

  return (
    <div className="three-board" ref={containerRef}>
      <p className="sr-only" role="img">{alt}. Semantic board with {board.width * board.height} terrain tiles, {board.cities.length} cities, and {board.unit_stacks.length} unit stacks.</p>
    </div>
  )
}
