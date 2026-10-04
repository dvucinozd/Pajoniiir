# Pioneer DDJ-400 — software profile

Adapted from kayrozen/Pajoniiir commit
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb`,
`controllers/pioneer_ddj_400/profile.json`. Recompiled using our S3CP v4 ABI;
the donor's extended v2 binary is deliberately not imported.

USB identity and MIDI addresses come from the donor. Its hardware reports
are provenance, not local qualification. **Hardware acceptance: NOT RUN.**
UAC format/routing/pacing support belongs to package F.

Beat FX target notes 94/10, 94/11 and 94/14 emit CH1, CH2 and BOTH on
press only. The DDJ-400 MASTER switch maps to our existing both-deck FX,
not a post-fader master bus. Channel filter bypasses the Smart CFX enable
gate using a profile capability; no controller-name checks are used.
Initial SysEx is emitted once per activation before LED feedback; VU scale
150 is applied using saturated integer arithmetic.

Eight donor memory-call/store/delete inputs lack current P4 semantic actions.
They remain explicitly listed under `omitted_inputs` in the source JSON and
are not assigned guessed actions. Memory cues themselves remain available
through the library/UI implementation.

Regenerate with:
`python tools/controller_profile/compile_profile.py controllers/pioneer_ddj_400/profile.json -o controllers/pioneer_ddj_400/profile.s3bin`.
