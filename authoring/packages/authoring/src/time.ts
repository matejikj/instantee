/**
 * Deterministic seconds → frame conversion shared with the native core-model
 * policy. Frames are end-exclusive intervals [frame, frame + 1).
 *
 * Rounding: nearest frame using half-away-from-zero on the exact rational
 * `seconds * fps.numerator / fps.denominator`. Silent `Number` truncation is
 * rejected; callers must go through these helpers.
 */

export type FrameRate = {
  readonly numerator: number;
  readonly denominator: number;
};

export type FrameConversion = {
  readonly frame: number;
  /** True when the chosen frame's start PTS does not equal the input seconds. */
  readonly subFrameLoss: boolean;
};

function assertFiniteNonNegative(seconds: number): void {
  if (!Number.isFinite(seconds) || seconds < 0) {
    throw new RangeError("seconds must be a finite non-negative number");
  }
}

function assertFrameRate(rate: FrameRate): void {
  if (
    !Number.isInteger(rate.numerator) ||
    !Number.isInteger(rate.denominator) ||
    rate.numerator < 1 ||
    rate.denominator < 1
  ) {
    throw new RangeError("frame rate must be positive integers");
  }
}

/**
 * Convert seconds to a zero-based frame index under the declared policy.
 * Uses integer BigInt arithmetic when seconds is an integer number of
 * microseconds (1e-6); otherwise falls back to a documented float path that
 * still rounds explicitly (never truncates toward zero silently).
 */
export function secondsToFrame(
  seconds: number,
  rate: FrameRate,
): FrameConversion {
  assertFiniteNonNegative(seconds);
  assertFrameRate(rate);

  // Prefer exact path for decimal-second inputs that are safe integers in µs.
  const micros = Math.round(seconds * 1_000_000);
  const exactMicros = Math.abs(seconds * 1_000_000 - micros) < 1e-9;
  let frame: number;
  if (exactMicros && Number.isSafeInteger(micros)) {
    // frame = round(micros * num / (1e6 * den)) with half-away-from-zero:
    // floor((2*x + denom) / (2*denom)) for non-negative x.
    const num = BigInt(rate.numerator);
    const den = BigInt(rate.denominator);
    const m = BigInt(micros);
    const scale = 1_000_000n * den;
    frame = Number((m * num * 2n + scale) / (scale * 2n));
  } else {
    const exact = (seconds * rate.numerator) / rate.denominator;
    frame = Math.sign(exact) * Math.round(Math.abs(exact));
  }

  if (!Number.isSafeInteger(frame) || frame < 0) {
    throw new RangeError("frame index is not a safe non-negative integer");
  }

  const ptsSeconds = (frame * rate.denominator) / rate.numerator;
  const subFrameLoss = Math.abs(ptsSeconds - seconds) > 1e-12;
  return { frame, subFrameLoss };
}

/** Inclusive start / exclusive end in frames for a seconds interval. */
export function secondsIntervalToFrames(
  startSeconds: number,
  endSeconds: number,
  rate: FrameRate,
): { start: number; endExclusive: number; subFrameLoss: boolean } {
  if (endSeconds < startSeconds) {
    throw new RangeError("endSeconds must be >= startSeconds");
  }
  const start = secondsToFrame(startSeconds, rate);
  const end = secondsToFrame(endSeconds, rate);
  return {
    start: start.frame,
    endExclusive: Math.max(end.frame, start.frame),
    subFrameLoss: start.subFrameLoss || end.subFrameLoss,
  };
}

export function durationSecondsToFrames(
  durationSeconds: number,
  rate: FrameRate,
): FrameConversion {
  return secondsToFrame(durationSeconds, rate);
}
