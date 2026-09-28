# M2.3 release decision — 2026-09-28

## Decision

Status: **APPROVED FOR EXACT-TAG BUILD AND RELEASE**.

The operator explicitly removed two remaining exact-release checks from the
M2.3 release scope because they are not currently relevant to the deployed
workflow:

- the real-Rekordbox cue A/C and loop slot/time comparison; and
- repetition of the duplicate raw track-ID hardware gate on the exact M2.3
  binary.

Both checks are recorded as **NOT RUN — OPERATOR ACCEPTED**, not as passes. The
duplicate-ID behavior retains its passing hardware evidence on the installed
`M2.2-37-g751d3c6` maintenance candidate. Rerun the relevant gate if cue import,
ANLZ parsing, loop persistence, media identity, persistent Hot Cue storage or
media lifecycle behavior becomes release-critical or changes later.

With that scope decision, no mandatory pre-tag functional gate remains for the
post-M2.2 maintenance candidate. The selected immutable release version is
`M2.3`. The tag must be created only on the clean, CI-green release commit and
must never be moved after publication.

## Evidence supporting the decision

The installed candidate `M2.2-37-g751d3c6` on `ota_0` passed:

- a 180.058-minute exact-image dual-deck soak with operator-confirmed clean
  MAIN and headphone cue;
- real-media load timing and catalog identity across controlled reboot/remount;
- slow-network Web Remote convergence and a physical-phone title, PLAY-state
  and visible MAIN-meter decay check;
- duplicate raw track-ID isolation across two independent Rekordbox exports,
  including separate Hot Cue state across remount and software reboot, with
  cleanup confirmed on both media.

A separate temporary instrumented image `M2.2-47-g07618a5`, with the normally
default-OFF PCM timeline scheduler probe enabled, passed 100 forced iterations.
The exact maintenance candidate was restored afterward and passed its paired
runtime smoke. The probe result is evidence for the timeline implementation;
it is not attributed to the installed `M2.2-37-g751d3c6` binary.

The complete qualification basis is recorded in
[`P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md`](P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md),
[`P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md`](P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md)
and
[`P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md`](P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md).

## Remaining release procedure

1. Merge this decision through CI and freeze the resulting clean `master`
   commit.
2. Create a local annotated `M2.3` tag on that exact commit.
3. Build with ESP-IDF v6.0.2 from the exact tag, sign with trusted key
   `rel-001`, and independently verify every artifact.
4. Install the signed bundle and verify the opposite slot, image identity,
   USB0/USB1 recovery, playback, audible MAIN/cue and strict health counters.
   The exact-image duplicate-ID repetition is accepted as unrun and must not be
   reported as a pass.
5. Push the immutable tag and publish the GitHub Release only after the tagged
   image passes.
6. Upload the versioned public OTA bundle first, independently fetch and hash
   it, then update and verify `latest.json` last.
