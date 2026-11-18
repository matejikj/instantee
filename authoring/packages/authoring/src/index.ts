/**
 * `@instantee/authoring` public surface v1.0.
 *
 * Import only from this package entry. Deep imports into `src/`, `generated/`,
 * or build paths are unsupported and break the freeze contract.
 */

export type * from "./generated/index.js";

export {
  INTENT_TYPES,
  createFilm,
  createIntent,
  filmToJson,
  type FilmInput,
  type ShotInput,
  type TextNodeInput,
  type IntentType,
  type SemanticIntentInput,
} from "./film.js";

export {
  collectNodeIds,
  parentIdOf,
  reparentNode,
  type ReparentOptions,
} from "./scene_graph.js";

export {
  secondsToFrame,
  secondsIntervalToFrames,
  durationSecondsToFrames,
  type FrameRate,
  type FrameConversion,
} from "./time.js";

export {
  AuthoringError,
  isAuthoringError,
  type AuthoringErrorCode,
} from "./errors.js";

export {
  PUBLIC_API_VERSION,
  STYLE_PACKAGES,
  STYLE_PACKAGE_IDS,
  DEPRECATIONS,
  deprecate,
  type StylePackageId,
  type DeprecationRecord,
} from "./policy.js";

export { direction } from "./direction.js";

import {
  createFilm,
  createIntent,
  filmToJson,
  INTENT_TYPES,
} from "./film.js";
import {
  collectNodeIds,
  parentIdOf,
  reparentNode,
} from "./scene_graph.js";
import {
  durationSecondsToFrames,
  secondsIntervalToFrames,
  secondsToFrame,
} from "./time.js";
import { AuthoringError, isAuthoringError } from "./errors.js";
import {
  DEPRECATIONS,
  PUBLIC_API_VERSION,
  STYLE_PACKAGE_IDS,
  STYLE_PACKAGES,
  deprecate,
} from "./policy.js";
import { direction } from "./direction.js";

/** Film construction namespace (aliases the flat createFilm helpers). */
export const film = Object.freeze({
  create: createFilm,
  toJson: filmToJson,
  createIntent,
  intentTypes: INTENT_TYPES,
});

/** Hierarchical scene-graph helpers. */
export const sceneGraph = Object.freeze({
  collectNodeIds,
  parentIdOf,
  reparent: reparentNode,
});

/** Shared seconds→frames conversion. */
export const time = Object.freeze({
  secondsToFrame,
  secondsIntervalToFrames,
  durationSecondsToFrames,
});

/** Locked V1 style package tags. */
export const styles = STYLE_PACKAGES;

/** Version, deprecation policy, and typed-error helpers. */
export const api = Object.freeze({
  version: PUBLIC_API_VERSION,
  styles: STYLE_PACKAGES,
  styleIds: STYLE_PACKAGE_IDS,
  deprecations: DEPRECATIONS,
  deprecate,
  AuthoringError,
  isAuthoringError,
  direction,
  film,
  sceneGraph,
  time,
});
