```
                  ┌──────────────────────────────────────────────┐
                  │              DOCUMENT  (files)                 │
                  │   parsing · serializer · blocks · roundtrip    │
                  │            · auto saving / recovery            │
                  └───────────────────┬────────────────────────────┘
                                      │ owns, saves off, greps 
   ┌──────────────────────────────────────────────────────────────────┐
   │   EDITING note  (ENG) — typed, deterministic, model-owned          │
   │   undos · blocks+wikilinks incremental · editor integrity ·        │
   │   recovery · the cached roundtrip · the renderer-of-renderers      │
   └───────┬───────────────────────────────────────────┬───────────────┘
           │                                             │
   ┌───────▼────────┐    schedules / backtracks   ┌──────▼─────────────┐
   │  COORDINATORS  │ ──────────────────────────► │  SEARCH / LOOKUP    │
   │  (Scheduler)   │ ◄────────────────────────── │  append-only index  │
   │ normalize·AST· │       reads / writes        │  log  +  collected  │
   │ blocks·reflows │                             │  link-graphs +      │
   └──┬──────────▲──┘                             │  backlinks          │
      │ frames   │ incorporates                   └─────────────────────┘
      ▼          │
 ┌─────────┐  ┌──┴────────────┐    each unit runs:
 │ PARSERS │  │   RENDERERS    │    block → tokenize → produce evidence
 │tokenize │─►│ layouts·paints │    → INCREMENTAL layout → gate
 │wordwraps│  │ incremental    │    (independence is enforced, not hoped)
 │prefetch │  │ repaints       │
 └────▲────┘  └────────────────┘
      │ proposes edits to presets / fonts / renderers / serialization
 ┌────┴─────────┐
 │   EXPORTER   │  bulk-work routed through the SAME undos, plus the
 │ (html-sink)  │  cached roundtrip, sample/staged preview, and recovery
 └──────────────┘
```