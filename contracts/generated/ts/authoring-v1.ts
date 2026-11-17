/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */

/**
 * Designated uint64 for seed and similar values. JSON numbers are limited to integers in [0, 9007199254740991] (JS Number safe range). Values above that MUST use a decimal string (no leading zeros except "0"). Consumers must reject decimal strings that overflow uint64; the schema alone cannot express 2^64-1 exactly for every draft-2020-12 validator.
 *
 * This interface was referenced by `InstanteeAuthoringManifestV1`'s JSON-Schema
 * via the `definition` "uint64_value".
 */
export type Uint64Value = number | string;
/**
 * Stable document identifier. UTF-8 allowed; U+0000, slash and backslash are rejected by language validators.
 *
 * This interface was referenced by `InstanteeAuthoringManifestV1`'s JSON-Schema
 * via the `definition` "id".
 */
export type Id = string;

/**
 * Structural contract for authoring documents. Semantic rules (unique IDs, typed references, uint64 overflow of decimal seeds, camera FOV for perspective) are enforced by language validators beyond JSON Schema.
 */
export interface InstanteeAuthoringManifestV1 {
  schema_version: "1.0.0";
  film_id: string;
  canvas: {
    width: number;
    height: number;
    color: "rec709-sdr";
  };
  frame_rate: {
    numerator: number;
    denominator: number;
  };
  seed: Uint64Value;
  style: string;
  assets?: {
    id: string;
    uri: string;
    sha256: string;
    media_type: string;
  }[];
  rigs?: {
    id: string;
    /**
     * Opaque rig interchange version; payload format lands later.
     */
    format_version: string;
    /**
     * Opaque versioned reference; not a native handle or memory dump.
     */
    payload_ref: string;
  }[];
  transitions?: {
    id: string;
    from_shot_id: string;
    to_shot_id: string;
    overlap_frames: number;
    blend: "cut" | "dissolve" | "wipe";
  }[];
  /**
   * @minItems 1
   */
  shots: [
    {
      id: string;
      duration_frames: number;
      style_override?: string;
      intent?: {
        type: "reveal" | "explain" | "compare" | "focus" | "build_suspense" | "resolve";
        intensity: number;
        importance?: number;
        target_ids?: string[];
      };
      nodes: Node[];
      camera?: {
        projection?: "orthographic" | "perspective";
        near?: number;
        far?: number;
        fov_y_rad?: number;
        preset?: string;
        transform?: {
          translate?: {
            x: number;
            y: number;
            z: number;
          };
          scale?: {
            x: number;
            y: number;
            z: number;
          };
          rotation_z_rad?: number;
          opacity?: number;
          pivot?: {
            x: number;
            y: number;
            z: number;
          };
        };
      };
    },
    ...{
      id: string;
      duration_frames: number;
      style_override?: string;
      intent?: {
        type: "reveal" | "explain" | "compare" | "focus" | "build_suspense" | "resolve";
        intensity: number;
        importance?: number;
        target_ids?: string[];
      };
      nodes: Node[];
      camera?: {
        projection?: "orthographic" | "perspective";
        near?: number;
        far?: number;
        fov_y_rad?: number;
        preset?: string;
        transform?: {
          translate?: {
            x: number;
            y: number;
            z: number;
          };
          scale?: {
            x: number;
            y: number;
            z: number;
          };
          rotation_z_rad?: number;
          opacity?: number;
          pivot?: {
            x: number;
            y: number;
            z: number;
          };
        };
      };
    }[]
  ];
}
/**
 * This interface was referenced by `InstanteeAuthoringManifestV1`'s JSON-Schema
 * via the `definition` "node".
 */
export interface Node {
  id: string;
  type: "group" | "shape" | "text" | "image" | "puppet" | "path";
  text?: string;
  asset_id?: string;
  rig_id?: string;
  shape?: "rect" | "circle" | "polygon" | "custom";
  transform?: {
    translate?: {
      x: number;
      y: number;
      z: number;
    };
    scale?: {
      x: number;
      y: number;
      z: number;
    };
    rotation_z_rad?: number;
    opacity?: number;
    pivot?: {
      x: number;
      y: number;
      z: number;
    };
  };
  material?: string;
  role?: string;
  visible?: boolean;
  children?: Node[];
  animations?: {
    property: "translate_x" | "translate_y" | "rotation_z" | "scale_x" | "scale_y" | "opacity";
    /**
     * @minItems 1
     */
    keyframes: [
      {
        frame: number;
        value: number;
      },
      ...{
        frame: number;
        value: number;
      }[]
    ];
  }[];
}
