import type { AuthoringManifestV1, AuthoringNode } from "./generated/index.js";
import { durationSecondsToFrames, type FrameRate } from "./time.js";

export type IntentType =
  | "reveal"
  | "explain"
  | "compare"
  | "focus"
  | "build_suspense"
  | "resolve";

export type TextNodeInput = {
  readonly id: string;
  readonly text: string;
  readonly role?: string;
  readonly x?: number;
  readonly y?: number;
  readonly opacity?: number;
};

export type ShotInput = {
  readonly id: string;
  /** Prefer durationFrames; durationSeconds uses the shared rounding policy. */
  readonly durationFrames?: number;
  readonly durationSeconds?: number;
  readonly intent?: {
    readonly type: IntentType;
    readonly intensity: number;
    readonly importance?: number;
    readonly targetIds?: readonly string[];
  };
  readonly nodes: readonly TextNodeInput[];
};

export type FilmInput = {
  readonly filmId: string;
  readonly width?: number;
  readonly height?: number;
  readonly frameRate?: FrameRate;
  readonly seed?: number | string;
  readonly style?: string;
  readonly shots: readonly ShotInput[];
};

function freezeDeep<T>(value: T): Readonly<T> {
  if (value !== null && typeof value === "object") {
    Object.freeze(value);
    for (const child of Object.values(value as object)) {
      freezeDeep(child);
    }
  }
  return value;
}

function buildNode(input: TextNodeInput): AuthoringNode {
  const node: AuthoringNode = {
    id: input.id,
    type: "text",
    text: input.text,
    visible: true,
    transform: {
      translate: { x: input.x ?? 0, y: input.y ?? 0, z: 0 },
      opacity: input.opacity ?? 1,
    },
  };
  if (input.role !== undefined) {
    node.role = input.role;
  }
  return node;
}

/**
 * Declarative Film builder. Produces a readonly authoring manifest. No GPU,
 * filesystem or render callbacks are attached to the result.
 */
export function createFilm(input: FilmInput): Readonly<AuthoringManifestV1> {
  if (!input.filmId) {
    throw new Error("filmId is required");
  }
  if (!input.shots.length) {
    throw new Error("at least one shot is required");
  }

  const frameRate = input.frameRate ?? { numerator: 30, denominator: 1 };
  const shots = input.shots.map((shot) => {
    let durationFrames = shot.durationFrames;
    if (durationFrames === undefined) {
      if (shot.durationSeconds === undefined) {
        throw new Error(`shot ${shot.id} needs durationFrames or durationSeconds`);
      }
      const converted = durationSecondsToFrames(shot.durationSeconds, frameRate);
      durationFrames = converted.frame;
      if (durationFrames < 1) {
        throw new RangeError(`shot ${shot.id} duration rounds to zero frames`);
      }
    }
    if (!Number.isInteger(durationFrames) || durationFrames < 1) {
      throw new RangeError(`shot ${shot.id} durationFrames must be a positive integer`);
    }

    const nodes = shot.nodes.map(buildNode);
    const entry: AuthoringManifestV1["shots"][number] = {
      id: shot.id,
      duration_frames: durationFrames,
      camera: { projection: "orthographic" },
      nodes,
    };
    if (shot.intent) {
      entry.intent = {
        type: shot.intent.type,
        intensity: shot.intent.intensity,
      };
      if (shot.intent.importance !== undefined) {
        entry.intent.importance = shot.intent.importance;
      }
      if (shot.intent.targetIds !== undefined) {
        entry.intent.target_ids = [...shot.intent.targetIds];
      }
    }
    return entry;
  });

  const [firstShot, ...restShots] = shots;
  if (firstShot === undefined) {
    throw new Error("at least one shot is required");
  }

  const manifest: AuthoringManifestV1 = {
    schema_version: "1.0.0",
    film_id: input.filmId,
    canvas: {
      width: input.width ?? 1080,
      height: input.height ?? 1920,
      color: "rec709-sdr",
    },
    frame_rate: {
      numerator: frameRate.numerator,
      denominator: frameRate.denominator,
    },
    seed: input.seed ?? 42,
    style: input.style ?? "editorial@1",
    assets: [],
    rigs: [],
    transitions: [],
    shots: [firstShot, ...restShots],
  };

  return freezeDeep(manifest);
}

/** Serialize a built film for the file/CLI boundary (no engine types). */
export function filmToJson(film: Readonly<AuthoringManifestV1>): string {
  return `${JSON.stringify(film, null, 2)}\n`;
}
