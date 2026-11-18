/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */

/**
 * 2D paper puppet skeletal interchange for Paper Cinema. Scene IR keeps opaque rig stubs (id/format_version/payload_ref); this schema defines the versioned payload. Supported retarget profile is only paper_2d_v1. Collection budgets (joints≤64, bones≤128, layers/skin≤32, influences≤4, constraints≤64, poses≤16) and graph/skin semantics are enforced by the CPU validator so generated TypeScript stays compact. No GPU handles.
 */
export interface InstanteePuppetRigInterchangeV1 {
  schema_version: "1.0.0";
  id: string;
  retarget: {
    /**
     * Only the curated 2D paper retarget subset is supported in v1.
     */
    profile: "paper_2d_v1";
    space: "2d";
    max_joints?: number;
    max_influences_per_vertex?: number;
  };
  /**
   * Joint hierarchy. Must be non-empty and ≤64; single-root DAG semantics are CPU-enforced.
   */
  joints: {
    id: string;
    parent_id?: string;
    pivot: Vec2;
  }[];
  /**
   * Optional bones (≤128). Endpoints must name distinct joints.
   */
  bones?: {
    id: string;
    from_joint_id: string;
    to_joint_id: string;
  }[];
  /**
   * Paper layers bound to joints (≤32).
   */
  layers?: {
    id: string;
    bind_joint_id: string;
  }[];
  /**
   * Per-layer skin influences (≤32 bindings, ≤4 influences, weights sum to 1).
   */
  skin_bindings?: {
    layer_id: string;
    influences: {
      joint_id: string;
      weight: number;
    }[];
  }[];
  /**
   * Supported constraint types are parent and hinge_limit only (≤64).
   */
  constraints?: {
    [k: string]: unknown | undefined;
  }[];
  /**
   * Named reference poses (≤16). Pose joint lists must be non-empty.
   */
  reference_poses?: {
    id: string;
    joints: {
      joint_id: string;
      rotation_z_rad: number;
      translate?: Vec2;
    }[];
  }[];
}
/**
 * This interface was referenced by `InstanteePuppetRigInterchangeV1`'s JSON-Schema
 * via the `definition` "vec2".
 */
export interface Vec2 {
  x: number;
  y: number;
}
