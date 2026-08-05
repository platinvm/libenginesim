/**
 * Unit helpers for writing engine definitions.
 *
 * The ABI is SI throughout, but nobody specifies an engine in metres and
 * radians. These convert from the units parts are actually sold in, and they
 * are the same conversions the built-in presets use.
 */

const M_PER_INCH = 0.0254;

export const units = {
  /* length -> metres */
  m: (v: number) => v,
  mm: (v: number) => v * 1e-3,
  cm: (v: number) => v * 1e-2,
  inch: (v: number) => v * M_PER_INCH,
  thou: (v: number) => v * M_PER_INCH * 1e-3,

  /* angle -> radians */
  deg: (v: number) => (v * Math.PI) / 180,
  rad: (v: number) => v,

  /* volume -> cubic metres */
  cc: (v: number) => v * 1e-6,
  litres: (v: number) => v * 1e-3,
  L: (v: number) => v * 1e-3,

  /* area -> square metres */
  cm2: (v: number) => v * 1e-4,
  inch2: (v: number) => v * M_PER_INCH * M_PER_INCH,

  /* mass -> kilograms */
  kg: (v: number) => v,
  g: (v: number) => v * 1e-3,
  lb: (v: number) => v * 0.45359237,

  /* torque -> newton-metres */
  Nm: (v: number) => v,
  lb_ft: (v: number) => v * 1.3558179483314004,

  /* A uniform disk about its centre, the usual stand-in for a flywheel. */
  diskMoment: (mass: number, radius: number) => 0.5 * mass * radius * radius,
  /* Upstream's thin-rod approximation for a connecting rod. */
  rodMoment: (mass: number, length: number) => (mass * length * length) / 12,
};
