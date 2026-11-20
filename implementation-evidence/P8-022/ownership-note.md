# Ownership / lifetime / concurrency

`spike-b05-worst-accepted-export` owns the RenderJob, pixel-buffer pool prewarm, Apple
VideoSink, OutputTransaction, and optional ProcessSampler/FrameSamples on the
calling thread with a process-wide stop_source. Complexity-tier fail-closed runs
before compile. Sampler and frame logs use bounded capacities; stop/wake/join
happens before child JSON publication. The Python harness only observes wait4
accounting and never reaps via poll.
