/**
 * Typed authoring-boundary errors. TypeScript remains a convenience layer;
 * C++ schema validation is still the security boundary.
 */
export type AuthoringErrorCode =
  | "invalid_argument"
  | "unsupported_intent"
  | "range_error"
  | "unknown_node"
  | "unknown_parent"
  | "cyclic_parent";

export class AuthoringError extends Error {
  readonly code: AuthoringErrorCode;
  readonly path?: string;

  constructor(
    code: AuthoringErrorCode,
    message: string,
    options: { readonly path?: string; readonly cause?: unknown } = {},
  ) {
    super(message, options.cause !== undefined ? { cause: options.cause } : undefined);
    this.name = "AuthoringError";
    this.code = code;
    if (options.path !== undefined) {
      this.path = options.path;
    }
  }
}

export function isAuthoringError(value: unknown): value is AuthoringError {
  return value instanceof AuthoringError;
}
