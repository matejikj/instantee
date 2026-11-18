import {
  createIntent,
  type IntentType,
  type SemanticIntentInput,
} from "./film.js";

type IntentOptions = {
  readonly importance?: number;
  readonly targetIds?: readonly string[];
};

function intent(
  type: IntentType,
  intensity: number,
  options: IntentOptions = {},
): SemanticIntentInput {
  return createIntent(type, intensity, options);
}

/**
 * Namespaced semantic-intent helpers for the public authoring surface.
 * Prefer these over bare stringly intent construction in application code.
 */
export const direction = Object.freeze({
  reveal: (intensity: number, options?: IntentOptions) =>
    intent("reveal", intensity, options),
  explain: (intensity: number, options?: IntentOptions) =>
    intent("explain", intensity, options),
  compare: (intensity: number, options?: IntentOptions) =>
    intent("compare", intensity, options),
  focus: (intensity: number, options?: IntentOptions) =>
    intent("focus", intensity, options),
  buildSuspense: (intensity: number, options?: IntentOptions) =>
    intent("build_suspense", intensity, options),
  resolve: (intensity: number, options?: IntentOptions) =>
    intent("resolve", intensity, options),
});
