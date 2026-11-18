/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */

/**
 * Versioned paper-sheet material knobs shared by Style Bible parameters and render-core. Ranges are closed; omitted fields use model defaults. No GPU handles or style-package names.
 */
export interface InstanteePaperMaterialParamsV1 {
  model_version: "1.0.0";
  parameters: {
    fiber?: number;
    roughness?: number;
    color_wash?: number;
    edge_thickness?: number;
    shadow_bias?: number;
    normal_variation?: number;
    seed?: number;
  };
  defaults?: {
    fiber: 0.4;
    roughness: 0.35;
    color_wash: 0.12;
    edge_thickness: 0.04;
    shadow_bias: 0.06;
    normal_variation: 0.15;
    seed: 1;
  };
}
