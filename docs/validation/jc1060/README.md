# JC1060 hardware validation

Hardware acceptance of `firmware/main-deck-jc1060` (Guition JC1060P470C_I_W_Y,
shared P4 core, DDJ-400). Software gates are already recorded elsewhere; every
scenario in [HARDWARE_TEST_PLAN.md](HARDWARE_TEST_PLAN.md) starts as **NOT RUN**.

- `HARDWARE_TEST_PLAN.md`: ordered test plan, pass criteria, stop conditions.
- `RESULT_TEMPLATE.md`: copy one per session into `evidence/<group>/`.
- `evidence/<group>/`: logs, `/api/status` + `/api/resources` captures, photos,
  one result file per session. Group letters match the plan (`O` = observability, run before `A`).

Rules:

- One result file per flashed image. Record board, project, source SHA and ELF
  SHA-256 (from `/api/status`); never mix results across images.
- A scenario is PASS only with evidence in this tree. Not run or waived stays
  NOT RUN / WAIVED with a reason; do not convert it.
- Keep large raw logs out of git when over ~1 MB; store the SHA-256 and the
  location instead.
- Reference behaviour from the previous fork (`Pajoniiir-fork-ref`, donor
  `428b97dd`, v323-v340) is a comparison aid, not acceptance evidence for this
  image.
