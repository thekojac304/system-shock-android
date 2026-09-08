# Development method

I define the goals, preservation scope and acceptance criteria. I test the reference hardware and decide which changes are retained.

ChatGPT assists with programming, calculations, debugging, automation and documentation. Changes are checked against the project requirements before adoption.

## Start from a known baseline

Record the source revision, dependencies and target configuration before changing them. Work in a separate copy when an experiment could affect the accepted build.

Keep original game data, signing keys and private credentials outside the public repository.

## Test the change at the right level

A build test checks whether the program can be produced. APK verification checks its package, architecture, files and signing properties.

Device tests check controls, display, sound, text entry and installation. A build result does not replace those observations.

Record the tested executable and separate automated results from manual acceptance. Describe untested conditions explicitly where they affect use or reproduction.

[Build checks](BUILD.md) · [Device compatibility](COMPATIBILITY.md) · [Release checklist](V1_RELEASE_GATE.md)

## Keep changes focused

Test one relevant change at a time where practical. Preserve a working baseline so an unsuitable experiment can be reversed.

A successful prototype may still fall outside the preservation scope. Widescreen, replacement graphics and font experiments remain in the development history.

[Preservation scope](PRESERVATION_SCOPE.md) · [Development history](DEVELOPMENT_HISTORY.md)

## Automate repeatable work

Use scripts for dependency setup, builds, file checks and repeatable tests. Validate a script before relying on it to change project state.

Keep design choices and final visual, audio and control acceptance separate from automated checks.

## Turn results into useful knowledge

Start each article with the problem or question it answers. Explain the change, its result and how the lesson can be reused.

Keep one main explanation for each topic. Other pages should link to it rather than maintain competing accounts.

Preserve useful failed attempts with their context. Update an article when later evidence changes the conclusion; keep historical versions identifiable.

[Knowledge base](KNOWLEDGE_BASE.md) · [Article template](kb/COMM_CELL_TEMPLATE.md)

## Write for the reader

Use short sentences and one idea per paragraph. Explain specialist terms when they first matter.

Describe the project directly. Keep drafting notes, corrections to earlier prose and internal task handovers out of product descriptions.

Write routine hardware descriptions and specifications in original wording. Add a product link when it helps readers check a detail, not an author–date citation after every fact.

Credit inherited code and specific contributions with names and source links. Use formal references where quotations, research findings or distinctive borrowed ideas need them.

Link project measurements to their test conditions and records. Keep significant limitations beside the result they qualify.

## Preserve continuity

Store source revisions, decisions, test results and next steps outside the conversation. Use Git history, release tags and versioned handovers to identify the accepted state.

Keep repository publication, website deployment and local project records as separate tasks. Confirm each before marking it complete.

## Review before publishing

Check that instructions match the current release. In particular, keep stable and pre-release package names, signing identities and installation paths distinct.

Check source links, units and compatibility statements. Preserve licence notices and upstream credits. Keep private data and keys out of every public attachment.

The aim is a reader who can understand the project, use the right instructions and inspect the evidence when needed.
