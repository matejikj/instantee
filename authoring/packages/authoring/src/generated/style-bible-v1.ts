/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */

/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "semver".
 */
export type Semver = string;
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "token_id".
 */
export type TokenId = string;

/**
 * Versioned creative-direction token document. Typed color, font, material, spacing, motion, camera and safe-area tokens. Optional epoch overlays retint the same package without a different renderer. No GPU handles, native paths as public URIs, or core-model mutations.
 */
export interface InstanteeStyleBibleV1 {
  schema_version: "1.0.0";
  package_id: string;
  version: Semver;
  minimum_engine_version: Semver;
  epochs?: {
    [k: string]: EpochOverlay | undefined;
  };
  tokens: {
    colors: {
      [k: string]: ColorToken | undefined;
    };
    fonts: {
      [k: string]: FontToken | undefined;
    };
    materials: {
      [k: string]: MaterialToken | undefined;
    };
    spacing: {
      [k: string]: SpacingToken | undefined;
    };
    motion: {
      [k: string]: MotionToken | undefined;
    };
    camera: {
      [k: string]: CameraToken | undefined;
    };
    safe_area: SafeAreaPolicy;
  };
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "epoch_overlay".
 */
export interface EpochOverlay {
  label: string;
  colors?: {
    [k: string]: ColorToken | undefined;
  };
  fonts?: {
    [k: string]: FontToken | undefined;
  };
  materials?: {
    [k: string]: MaterialToken | undefined;
  };
  spacing?: {
    [k: string]: SpacingToken | undefined;
  };
  motion?: {
    [k: string]: MotionToken | undefined;
  };
  camera?: {
    [k: string]: CameraToken | undefined;
  };
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "color_token".
 */
export interface ColorToken {
  space: "linear-rgba";
  r: number;
  g: number;
  b: number;
  a: number;
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "font_token".
 */
export interface FontToken {
  family: string;
  role: "headline" | "subhead" | "body" | "caption" | "mono";
  size_scale: number;
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "material_token".
 */
export interface MaterialToken {
  shader_feature: string;
  opacity: number;
  parameters?: {
    [k: string]: number | undefined;
  };
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "spacing_token".
 */
export interface SpacingToken {
  px: number;
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "motion_token".
 */
export interface MotionToken {
  ease: "linear" | "ease_in" | "ease_out" | "ease_in_out" | "spring";
  duration_frames: number;
  intensity: number;
  stagger_frames?: number;
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "camera_token".
 */
export interface CameraToken {
  framing: "static" | "push_in" | "pull_out" | "pan" | "orbit";
  ease?: "linear" | "ease_in" | "ease_out" | "ease_in_out" | "spring";
}
/**
 * This interface was referenced by `InstanteeStyleBibleV1`'s JSON-Schema
 * via the `definition` "safe_area_policy".
 */
export interface SafeAreaPolicy {
  mode: "reels_overlay" | "reels_clear" | "full_frame";
  top_px: number;
  bottom_px: number;
  left_px: number;
  right_px: number;
}
