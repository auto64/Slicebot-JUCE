# DO NOT BREAK AUDIO — HARD RULES

This project is a working JUCE audio engine.

The following rules are ABSOLUTE:

1. DO NOT change existing audio routing behavior.
2. DO NOT modify audio device selection logic.
3. DO NOT add allocations on the audio thread.
4. DO NOT refactor for style, clarity, or “cleanup”.
5. DO NOT change timing, buffering, or callback order unless explicitly instructed.
6. DO NOT remove code, even if it looks unused.
7. DO NOT merge features or “improve architecture”.

Allowed actions ONLY when explicitly instructed:
- Add new functionality in parallel
- Read and summarize behavior
- Add comments
- Add new files that do not affect existing behavior

If any instruction is ambiguous:
STOP.
DO NOT GUESS.
ASK FIRST.
