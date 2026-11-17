/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */

/**
 * Designated uint64 for seed and similar values. JSON numbers are limited to integers in [0, 9007199254740991] (JS Number safe range). Values above that MUST use a decimal string (no leading zeros except "0"). Consumers must reject decimal strings that overflow uint64; the schema alone cannot express 2^64-1 exactly for every draft-2020-12 validator.
 *
 * This interface was referenced by `InstanteeSceneIRV1`'s JSON-Schema
 * via the `definition` "uint64_value".
 */
export type Uint64Value = number | string;
/**
 * This interface was referenced by `InstanteeSceneIRV1`'s JSON-Schema
 * via the `definition` "id".
 */
export type Id = string;

/**
 * Platform-neutral Scene IR. Cross-reference resolution, DAG checks, overlap, keyframe order, finite floats, camera FOV rules, and asset integrity are enforced beyond JSON Schema. No raw pointers, callbacks, or native handles.
 */
export interface InstanteeSceneIRV1 {
  schema_version: "1.0.0";
  compiler_version: string;
  canvas: {
    width: number;
    height: number;
    color: "rec709-sdr";
  };
  frame_rate: {
    numerator: number;
    denominator: number;
  };
  duration_frames: number;
  seed: Uint64Value;
  assets: {
    id: string;
    uri: string;
    sha256: string;
    media_type: string;
    color_profile?: "srgb" | "rec709" | "linear-rec709" | "untagged";
  }[];
  materials: {
    id: string;
    shader_feature: string;
    opacity: number;
    parameters?: {
      [k: string]: number | undefined;
    };
  }[];
  geometry: {
    id: string;
    kind: "path" | "rectangle" | "ellipse" | "polygon" | "stroke";
    points?: never[][];
    /**
     * @minItems 4
     * @maxItems 4
     */
    bounds?: never[];
    closed?: boolean;
  }[];
  text_runs: {
    id: string;
    text: string;
    font_asset_id: string;
    font_size_px: number;
    max_width_px?: number;
    text_role?: string;
  }[];
  cameras?: {
    id: string;
    projection: "orthographic" | "perspective";
    near?: number;
    far?: number;
    /**
     * Required for perspective projection (semantic rule).
     */
    fov_y_rad?: number;
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
  nodes: {
    id: string;
    kind: "group" | "geometry" | "text" | "image" | "puppet" | "camera";
    parent_id?: string;
    layer_order: number;
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
    geometry_id?: string;
    text_run_id?: string;
    asset_id?: string;
    material_id?: string;
    camera_id?: string;
    rig_id?: string;
    visible?: boolean;
  }[];
  animation_tracks: {
    id: string;
    target_id: string;
    property: "translate_x" | "translate_y" | "translate_z" | "scale_x" | "scale_y" | "rotation_z_rad" | "opacity";
    interpolation?: {
      type: "hold" | "linear" | "cubic_bezier";
      /**
       * @minItems 4
       * @maxItems 4
       */
      control_points?: [number, number, number, number];
    };
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
  /**
   * @minItems 1
   */
  shots: [
    {
      id: string;
      start_frame: number;
      duration_frames: number;
      node_ids: string[];
      camera_node_id?: string;
    },
    ...{
      id: string;
      start_frame: number;
      duration_frames: number;
      node_ids: string[];
      camera_node_id?: string;
    }[]
  ];
  transition_spans?: {
    id: string;
    from_shot_id: string;
    to_shot_id: string;
    start_frame: number;
    duration_frames: number;
    blend: "cut" | "dissolve" | "wipe";
  }[];
  provenance: {
    project_sha256: string;
    style_packages: {
      id: string;
      version: string;
    }[];
    source_schema_version?: string;
  };
}
