# Ownership / lifetime / concurrency

Each style export binary owns RenderJob, pixel-buffer pool prewarm, Apple
VideoSink, OutputTransaction, and optional ProcessSampler/FrameSamples on the
calling thread with a process-wide stop_source. The Python baselines only
observe wait4 accounting. The certificate assembler is pure validation over
retained baseline documents and introduces no shared mutable state.
