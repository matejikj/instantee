# Apple export adapter compatibility matrix

Probes locked deployment/SDK versions and AVFoundation writer capabilities.
V1 selects timed CMSampleBuffer append for exact FrameClock durations.
Modern `pixel_buffer_receiver` is detected on macOS 26+ but reported as
unsupported for the ObjC++ V1 binding until explicitly adopted. Deprecated
`pixel_buffer_adaptor` is never selected. Writer failures for existing paths
and invalid geometry stay fail-closed.

Measurement: none. Evidence tier B (native + ASan). Timing risk includes PTS.
