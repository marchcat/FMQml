# Next Feature Work Plan

This document defines the next three product features for FM:

1. cross-platform Open With and application associations;
2. folder comparison and synchronization;
3. duplicate-file discovery and cleanup.

The goal is to make each track directly implementable. The document records
product decisions, ownership boundaries, proposed C++ and QML surfaces,
implementation phases, failure handling, and acceptance checks.

Project guidance used by this plan:

- keep filesystem, launch, traversal, hashing, and synchronization policy in
  C++;
- keep QML responsible for presentation and user intent, not platform discovery
  or destructive-operation policy;
- preserve provider and archive boundaries;
- keep long-running work asynchronous, cancellable, generation-guarded, and
  independent from panel navigation;
- reuse OperationQueue for mutations instead of creating side-channel copy,
  move, or delete implementations;
- never turn an inferred comparison or duplicate match directly into a
  destructive operation without a reviewable plan.

## Work Order

Recommended order:

1. Open With.
   - It extends the existing LaunchService and is the smallest isolated feature.
   - It replaces the separate Wine/Proton menu exception with one application
     selection model.
2. Folder Compare and Sync.
   - It provides the largest two-panel workflow improvement.
   - It establishes a reviewed-plan execution pattern that can later be reused
     by duplicate cleanup.
3. Find Duplicates.
   - It reuses the existing native recursive traversal work.
   - It adds shared fingerprinting primitives useful for strict folder content
     comparison.

Folder Compare may initially use direct byte comparison for explicit strict
checks. When Find Duplicates introduces the shared fingerprint service, Folder
Compare should migrate to it rather than keeping a second hashing pipeline.

# 1. Cross-Platform Open With

## Goal

Provide a complete application chooser for local files on every supported
desktop platform.

The feature must:

- show applications capable of opening the selected file type;
- allow one-time launch with a selected application;
- allow FM-specific per-type application preferences;
- allow returning to the system default;
- preserve the existing normal Open behavior;
- integrate Open with Wine and Open with Steam Proton into the same model on
  Linux;
- keep executable security and provider boundaries intact.

Windows and Linux are release gates for the current project. The architecture
must also contain a macOS backend so Open With does not become another
Windows/Linux conditional embedded in FilePanelController.

## Current State

- LaunchService already owns local path validation, launch classification,
  structured errors, Windows shell launch, Linux native executable handling,
  Wine launch, and Steam Proton launch.
- FilePanelController exposes launch capabilities when the context menu opens.
- FilePanelContextMenu currently contains separate Open, Open with Wine, and
  Open with Steam Proton actions.
- SteamProtonLaunchDialog already owns Proton runtime and per-launch options.
- Normal documents use the platform default application.
- Generic application discovery, application selection, FM-specific
  associations, and a reusable application model do not exist.
- Remote provider and archive virtual paths are intentionally blocked from
  native launch.

## Product Decisions

- Open remains the fast default action.
- Open With opens a custom FM dialog with recommended and available
  applications.
- The dialog offers:
  - Open once;
  - Always use in FM for this file type;
  - Use system default.
- FM-specific preferences do not silently change the operating-system default.
- A separate platform action may open the native system association UI when
  available.
- Wine and Steam Proton are runner entries, not fake desktop applications.
- Normal Linux Open for a Windows application remains blocked. Its Open With
  dialog contains Wine and installed Proton runtimes.
- Open With does not bypass executable-bit, desktop-launcher trust, SmartScreen,
  UAC, Mark-of-the-Web, or provider restrictions.
- The first implementation accepts one selected file. Homogeneous
  multi-selection is added after single-file behavior is stable.
- Remote/provider materialization is out of scope. Open With is available only
  for a real local file or an explicitly supported managed local path.

## Terminology

- Application candidate: an installed application advertised for a content
  type.
- Runner candidate: a compatibility runtime such as Wine or Proton.
- System default: the handler selected by the operating system.
- FM preference: an application choice stored by FM without changing the system
  association.
- Content type key: a platform-neutral preference key derived from MIME type,
  filename suffix, and launch category.
- Launch target: a validated local file plus known MIME, suffix, and launch
  classification.

## Architecture

### OpenWithService

Add src/core/OpenWithService.h and src/core/OpenWithService.cpp.

Responsibilities:

- validate and normalize the launch target;
- determine MIME type, suffix, and LaunchService category;
- request application candidates from the active platform backend;
- merge platform applications with runner candidates;
- order and de-duplicate candidates;
- resolve the effective default;
- launch with a selected candidate;
- persist and clear FM-specific preferences;
- expose structured errors and diagnostics.

Suggested public data:

- OpenWithTarget
  - path;
  - displayName;
  - mimeType;
  - suffix;
  - contentTypeKey;
  - LaunchCategory;
  - local and launchable flags;
  - blocked reason.
- OpenWithCandidate
  - stable id;
  - display name;
  - icon name or icon source;
  - kind: application, Wine, Proton, system chooser;
  - backend payload kept in C++;
  - recommended flag;
  - system-default flag;
  - FM-default flag;
  - supports multiple files;
  - availability and unavailable reason.
- OpenWithResult
  - success;
  - error code;
  - title;
  - message;
  - details;
  - show-dialog flag.

Suggested service surface:

- targetInfo(path);
- candidatesForPath(path);
- openWith(path, candidateId);
- openWithMany(paths, candidateId);
- setPreferredCandidate(path, candidateId);
- clearPreferredCandidate(path);
- effectiveCandidate(path);
- openSystemAssociationUi(path).

Candidate discovery must happen when the context menu or dialog requests it.
Do not query installed applications for every visible file delegate.

### Platform Backends

Define a narrow backend interface under src/platform/openwith:

- enumerateCandidates(target);
- launch(targets, candidate);
- systemDefaultCandidate(target);
- openSystemAssociationUi(target);
- resolveCandidateIcon(candidate).

Backend objects own native handles and platform identifiers. QML must never
receive registry command strings, desktop Exec strings, COM pointers, or
LaunchServices objects.

### Preference Store

Add an OpenWithPreferenceStore using QSettings.

Preference key resolution order:

1. launch-category-specific key for special executable categories;
2. normalized MIME type;
3. suffix fallback when MIME is generic or unknown;
4. generic unknown-file key only when explicitly selected by the user.

Stored value:

- backend kind;
- stable candidate id;
- optional display name for diagnostics;
- schema version.

On startup or use:

- verify that the selected candidate still exists;
- ignore stale preferences and fall back to the system default;
- do not delete stale data until it has been observed and logged, so an
  application temporarily unavailable on a removable installation does not
  lose the user's choice immediately.

Settings export/import should include FM Open With preferences in a dedicated
section. Import must not import platform-specific choices onto a different OS.

## Windows Backend

Use shell association APIs rather than parsing executable command strings.

Candidate enumeration:

- use SHAssocEnumHandlers for the selected extension or association;
- enumerate recommended handlers first and then remaining valid handlers;
- obtain stable handler identity, display name, and icon where available;
- de-duplicate handlers that resolve to the same registered application;
- include the current system default at the top.

Launching:

- prefer IAssocHandler invocation or the equivalent shell-owned invocation path;
- preserve shell security behavior and a valid parent HWND;
- do not construct an unquoted command line from registry values;
- preserve file association behavior for packaged and classic applications.

Native association UI:

- use SHOpenWithDialog or the supported shell Open With entry point;
- setting the Windows system default remains owned by Windows;
- never write protected UserChoice registry hashes.

Acceptance details:

- classic Win32, packaged applications, and applications with spaces in their
  paths appear and launch correctly;
- an uninstalled stale handler is hidden or reported unavailable;
- downloaded executables still pass through SmartScreen and UAC behavior;
- unknown extensions can open the native system chooser.

## Linux Backend

Use MIME and desktop-entry semantics.

Candidate discovery:

- determine MIME with QMimeDatabase;
- read mimeapps.list files using XDG precedence;
- enumerate desktop files under XDG_DATA_HOME and XDG_DATA_DIRS;
- respect Hidden, NoDisplay, Type, TryExec, OnlyShowIn, and NotShowIn;
- match the MimeType list;
- resolve localized Name and Icon;
- de-duplicate by desktop-file id.

Launching:

- parse desktop Exec safely;
- support the required field codes: %f, %F, %u, %U, %i, %c, and %k;
- never invoke a shell to expand the Exec field;
- honor Terminal through the existing TerminalLauncher;
- pass one or several paths according to the advertised field code;
- set the working directory only when desktop-entry semantics request it.

System default:

- resolve through mimeapps.list precedence;
- Use system default clears the FM preference;
- an optional Set system default action may call xdg-mime through QProcess with
  separate arguments or update the correct user mimeapps.list through a
  dedicated safe writer;
- do not change the system default as a side effect of Always use in FM.

Wine and Proton integration:

- for LaunchCategory::WindowsApplication, add Wine when available;
- add one candidate per discovered Proton runtime or one Steam Proton candidate
  that opens the existing runtime-options dialog;
- retain vkBasalt, logging, XMODIFIERS, and runtime selection in
  SteamProtonLaunchDialog;
- a missing runner may remain visible as an unavailable candidate with a useful
  installation/configuration message;
- native Linux document handlers must not be offered as executable runners for
  PE applications.

## macOS Backend

Add an Objective-C++ backend compiled only on macOS.

- use LaunchServices or NSWorkspace to enumerate applications for the file URL
  or content type;
- use bundle identifier as the stable candidate id;
- resolve application name and icon through the native workspace APIs;
- open the file with the selected application through NSWorkspace;
- open the native association workflow when supported;
- keep platform code in .mm files and enable OBJCXX only for the Apple build.

If macOS remains outside current CI/build targets, the generic service must
still compile without the backend and return UnsupportedPlatform cleanly.

## Controller and UI Integration

Add OpenWithController or expose OpenWithService through AppServices.

Add qml/components/OpenWithDialog.qml using the existing DialogShell family.

Dialog contents:

- target file name and type;
- system-default application;
- recommended applications;
- all compatible applications;
- Wine/Proton runner section when applicable;
- Open button;
- Always use in FM checkbox or secondary action;
- Use system default action;
- native system chooser action where available;
- clear unavailable-candidate explanation.

Context menu:

- keep Open;
- replace the two standalone Wine/Proton actions with Open With;
- optionally show the effective preferred application as Open with <name>;
- avoid building a large dynamic submenu before candidate discovery completes.

Command palette:

- Open with application;
- Reset Open With preference for current type.

Errors must use the same structured launch error presentation already used by
LaunchService.

## Multi-Selection

Phase 1 supports one file.

Phase 2 permits multiple selected local files only when:

- all items share a compatible content type or launch category;
- the candidate advertises multi-file support, or FM can safely invoke it once
  per file;
- the selection does not mix documents and executables;
- the item count is bounded or confirmed before launching many processes.

Desktop %F/%U and native multi-file APIs should receive one invocation.
Candidates supporting only %f/%u are launched once per file with a conservative
process-count limit.

## Implementation Phases

### Phase 1: Generic model and preference store

1. Add OpenWithTarget, OpenWithCandidate, and OpenWithResult.
2. Add OpenWithService and backend interface.
3. Add content-type key generation and preference persistence.
4. Route errors through existing launch result presentation.
5. Keep existing Open and Wine/Proton actions working during migration.

Verify:

- target classification matches LaunchService;
- preferences resolve and clear correctly;
- stale candidate ids fall back to the system default;
- provider and archive paths remain blocked.

### Phase 2: Windows backend

1. Enumerate association handlers.
2. Resolve default, names, and icons.
3. Invoke selected handlers with shell ownership and parent HWND.
4. Add native system chooser.
5. Add focused backend tests around result mapping and stable ids.

### Phase 3: Linux backend and runner unification

1. Implement XDG MIME application discovery.
2. Implement safe desktop Exec expansion.
3. Integrate TerminalLauncher.
4. convert Wine and Proton into runner candidates.
5. migrate FilePanelContextMenu to the generic Open With action.

### Phase 4: Dialog, command palette, and settings

1. Add OpenWithDialog.
2. Add FM preference controls.
3. Include preferences in settings export/import.
4. Add command palette commands.
5. add homogeneous multi-selection.

### Phase 5: macOS backend and polish

1. Add LaunchServices/NSWorkspace backend.
2. Add icon caching and candidate discovery caching.
3. Invalidate caches on application-installation changes where practical.
4. complete cross-platform manual verification.

## Open With Acceptance Matrix

- Windows document opens once with a non-default classic application.
- Windows packaged application appears and launches.
- Windows native chooser opens for an unknown extension.
- Always use in FM changes only FM behavior.
- Use system default restores normal shell behavior.
- Linux MIME handlers follow XDG precedence.
- Linux desktop Exec paths and filenames containing spaces remain separate
  arguments.
- Terminal desktop applications use TerminalLauncher.
- Linux Windows application shows Wine and Proton candidates but does not run
  from normal Open.
- Missing Wine or Proton produces a clear unavailable-runner message.
- macOS candidates use bundle identifiers and launch through NSWorkspace.
- provider and archive virtual paths never reach native Open With backends.
- changing panel selection while the dialog is open does not change its target.

# 2. Folder Compare and Synchronization

## Goal

Compare the current left and right panel locations, present differences clearly,
and build an explicit synchronization plan that the user can review before any
file operation begins.

The feature must support:

- immediate comparison of the two current folders;
- optional recursive comparison;
- metadata comparison and optional strict content comparison;
- clear left-only, right-only, newer, different, equal, conflict, and error
  states;
- filtering to differences;
- one-way and two-way synchronization planning;
- per-item action overrides;
- safe execution through OperationQueue;
- stale-plan detection when files change after comparison.

## Product Decisions

- Compare and Sync requires split view and two valid browsable panel locations.
- Compare is read-only.
- Synchronization never starts directly from inferred differences.
- Every synchronization is represented by a previewable plan.
- Delete actions are disabled by default.
- Mirror mode may propose permanent deletions, but it requires an explicit
  destructive option and the existing permanent-delete confirmation path.
- Metadata comparison is the default fast mode.
- Strict content comparison is opt-in and hashes only candidate pairs.
- Symlink targets are compared as links; directory symlinks are not traversed.
- Rename detection is not part of the first implementation.
- Initial implementation targets local-to-local paths. The engine and models
  retain provider identity so provider comparison can be added without a UI
  rewrite.

## Comparison States

Each relative path has one state:

- EqualMetadata: type, size, and normalized timestamp match.
- EqualContent: content verification completed and matched.
- LeftOnly.
- RightOnly.
- LeftNewer.
- RightNewer.
- DifferentSize.
- DifferentContent.
- TypeConflict: file versus directory or incompatible provider type.
- LinkConflict: symlink target or link kind differs.
- InaccessibleLeft.
- InaccessibleRight.
- Skipped.
- ChangedAfterCompare.

Directory summary state is derived from descendants and is never used as proof
that file content matches.

## Comparison Options

- recursive;
- include hidden;
- compare timestamps;
- timestamp tolerance;
- compare file contents;
- follow mount boundaries;
- include symlinks as link entries;
- case sensitivity policy;
- ignore patterns;
- minimum or maximum file size for strict hashing;
- show equal items;
- show only actionable differences.

Defaults:

- recursive off for the first instant comparison;
- hidden follows the panel setting;
- timestamps enabled;
- tolerance automatically accounts for filesystem granularity, with a default
  maximum of two seconds;
- content hashing off;
- mount-boundary traversal off;
- symlink traversal off;
- delete proposals off.

## Architecture

### FolderCompareController

Add src/controllers/FolderCompareController.

Properties:

- state: Idle, Scanning, ComparingContent, Finished, Canceling, Failed,
  PlanReady, Executing;
- leftRoot and rightRoot;
- options;
- progress;
- scanned counts and compared bytes;
- skipped and inaccessible counts;
- result model;
- sync plan model;
- stale result flag;
- error and diagnostics.

Invokables:

- canCompare(leftPath, rightPath);
- compare(leftPath, rightPath, options);
- cancel();
- clear();
- buildPlan(mode, options);
- setPlannedAction(row, action);
- resetPlannedActions();
- executePlan();
- refreshChangedItems();

The controller owns one generation. Results from an older comparison must not
modify a new comparison.

### FolderCompareScanner

Add src/core/FolderCompareScanner.

Responsibilities:

- enumerate both roots asynchronously;
- build entries keyed by normalized relative path;
- compare metadata;
- schedule strict content work only for candidate pairs;
- emit batches for progressive UI;
- collect inaccessible/skipped diagnostics;
- honor cancellation and mount boundaries.

Use LinuxFileEnumerator on Linux. Extract or reuse the Windows native recursive
enumeration pattern from FileSearchScanner rather than introducing
QDirIterator as the final Windows hot path.

Do not reuse DirectoryModel contents as the source of truth. A panel may be
filtered, partially loaded, or changing while comparison runs.

### Relative Path and Case Policy

Store the original relative path for each side and a comparison key.

- Windows local paths compare case-insensitively but preserve displayed case.
- Linux local paths compare case-sensitively.
- macOS policy follows the actual volume when detectable and otherwise uses a
  conservative platform default.
- providers eventually supply their own case policy.

Case collisions must produce a conflict, not silently collapse two entries.

### Timestamp Policy

Different filesystems expose different timestamp precision.

- normalize timestamps to UTC;
- compare with the configured tolerance;
- infer a safe tolerance from both filesystems when possible;
- never classify a file as newer when the difference is inside tolerance;
- when size matches and timestamps fall inside tolerance, classify as
  EqualMetadata unless strict content comparison is requested;
- expose timestamp ambiguity in row details.

### Content Verification

For strict mode:

1. compare type and size;
2. for non-empty equal-size files, compute or request a shared fingerprint;
3. if fingerprints differ, mark DifferentContent;
4. if fingerprints match, mark EqualContent;
5. before a destructive sync action, optionally perform byte verification when
   the decision depends only on a sampled quick fingerprint.

Folder Compare should eventually consume the shared FileFingerprintService
defined in the Duplicate Finder track.

### FolderCompareModel

Add a QAbstractListModel with roles:

- relativePath;
- leftPath and rightPath;
- left and right display names;
- left and right type;
- left and right size;
- left and right modified time;
- comparison state;
- suggested action;
- selected planned action;
- reason;
- depth;
- parent relative path;
- directory flag;
- expanded flag;
- actionable flag;
- warning flag;
- stale flag.

The model supports filtering and sorting without rebuilding comparison results.

### Sync Plan

Represent synchronization with immutable plan items:

- action: None, CreateDirectoryLeft, CreateDirectoryRight, CopyLeftToRight,
  CopyRightToLeft, DeleteLeft, DeleteRight;
- source and destination;
- expected source metadata;
- expected destination metadata or expected absence;
- reason;
- destructive flag;
- estimated bytes;
- dependency ids.

Modes:

- Update left from right;
- Update right from left;
- Two-way newest;
- Custom;
- Mirror left to right;
- Mirror right to left.

Two-way newest never guesses when timestamps are equal/ambiguous and content
differs. Such rows remain unresolved.

Default rules:

- left-only/right-only items copy toward the selected destination in one-way
  mode;
- newer source replaces older destination;
- type conflicts remain unresolved;
- deletes remain None unless mirror deletion is explicitly enabled;
- inaccessible and stale rows remain blocked.

### Plan Revalidation

Immediately before execution, re-stat every actionable item.

Verify:

- source still exists;
- source type, size, and modified time match the comparison snapshot;
- expected destination still exists or remains absent;
- strict-content source has not changed since hashing;
- parent destination remains writable;
- provider capabilities still permit the action.

Changed items become ChangedAfterCompare and are removed from execution. The
user may refresh them or rerun comparison.

### Execution

Add a SyncPlanExecutor or an OperationQueue batch-plan request.

The executor must use OperationQueue for all mutations.

Order:

1. create destination directories from shallow to deep;
2. copy files;
3. apply directory metadata only if supported;
4. perform deletes last;
5. delete children before parent directories.

Requirements:

- reuse conflict resolution;
- preserve cancel and progress behavior;
- show aggregate items and bytes;
- do not execute a later dependent action after an earlier required action
  fails;
- return per-item results;
- refresh both panels and tree paths after completion;
- record compatible actions in HistoryManager where meaningful.

Do not enqueue hundreds of unrelated public OperationQueue requests from QML.
Keep plan identity and aggregate status in C++.

## UI

Add qml/components/FolderCompareDialog.qml.

Layout:

- left and right root headers;
- compare-mode and recursion controls;
- summary counts;
- filter chips for each state;
- virtualized result list;
- left metadata, relative path/state, and right metadata columns;
- per-row action selector;
- plan summary with copy counts, delete counts, unresolved conflicts, and bytes;
- Compare, Build Plan, Cancel, Synchronize, and Close actions.

Visual treatment:

- reuse semantic Theme tokens;
- use small state badges, not full-row saturated colors;
- preserve readable differences in dark and light themes;
- support keyboard navigation;
- allow opening or revealing either side of a row;
- allow previewing left or right file through the existing preview flow.

Panel integration:

- command palette: Compare panel folders;
- toolbar or panel menu action when split mode is active;
- optional status-bar indicator while comparison results are active;
- disable when either panel is a virtual root or unsupported provider path.

## Provider Expansion

After local-to-local is stable:

- use FileProvider metadata and capabilities;
- compare provider paths without downloading content in metadata mode;
- strict content comparison is available only when a provider supplies a stable
  checksum or explicitly permits bounded materialization;
- local-to-provider synchronization uses existing transfer methods;
- provider-to-provider synchronization uses cleanup-managed staging only when
  direct transfer is unavailable;
- respect provider trash/delete semantics and read-only containers;
- report unsupported action per row rather than disabling the entire compare.

## Implementation Phases

### Phase 1: Local metadata comparison

1. Add result types, options, scanner, controller, and model.
2. Implement non-recursive and recursive local enumeration.
3. Implement relative-path, case, type, size, and timestamp comparison.
4. Add cancellation, generation guards, and diagnostics.
5. Add a read-only comparison dialog.

### Phase 2: Filtering and strict comparison

1. Add state filters and sorting.
2. Add strict content comparison.
3. Add timestamp tolerance and case-collision handling.
4. Add symlink and mount-boundary behavior.
5. Add reveal/open/preview actions.

### Phase 3: Synchronization planning

1. Add plan types and modes.
2. Generate safe default actions.
3. Add per-row overrides and unresolved conflict state.
4. Add plan summary and destructive-action gating.
5. Add plan revalidation.

### Phase 4: Execution

1. Add SyncPlanExecutor or OperationQueue plan support.
2. Execute dependency-ordered actions.
3. report per-item failures and aggregate progress.
4. integrate conflict resolution, history, refresh, and cancellation.
5. add mirror-mode permanent-delete confirmation.

### Phase 5: Provider support and polish

1. Add provider metadata comparison.
2. Add capability-aware plan actions.
3. Add provider checksum support where available.
4. add command palette and toolbar integration.
5. persist only UI preferences, never stale comparison results.

## Folder Compare Acceptance Matrix

- non-recursive comparison classifies files present on one or both sides.
- recursive comparison handles deep trees without blocking the UI.
- file-versus-directory conflict is never auto-resolved.
- FAT-like timestamp differences inside tolerance do not produce false newer
  states.
- strict comparison detects equal-size different-content files.
- symlink loops are not traversed.
- mount boundaries are skipped by default.
- cancel stops scanning and stale batches do not repopulate results.
- one-way plan copies only in the requested direction.
- two-way newest leaves ambiguous conflicts unresolved.
- mirror deletions are absent unless explicitly enabled.
- changing a source after comparison blocks its planned action.
- failed parent directory creation blocks dependent copies.
- navigation remains usable while comparison runs.
- both panels refresh after execution.

# 3. Find Duplicates

## Goal

Find exact duplicate local files efficiently, group them clearly, estimate
reclaimable space accurately, and let the user review selected files before
using the normal permanent-delete workflow.

The feature must:

- scan one or several local roots;
- avoid hashing files that cannot be duplicates;
- remain responsive and cancellable;
- identify hard-linked files so they are not counted as reclaimable duplicates;
- verify exact equality before presenting a destructive cleanup plan;
- provide useful selection helpers without automatically deciding what to keep;
- reuse the existing delete confirmation and OperationQueue.

## Product Decisions

- The first implementation finds exact content duplicates, not similar images,
  similar names, or fuzzy media matches.
- Scope may be the active folder, selected folders, or both panel roots.
- Local paths are supported first. Remote providers are excluded by default to
  avoid uncontrolled downloads and API usage.
- Symlinks are listed only when explicitly requested and are never followed as
  directory roots.
- Hard links to the same physical file are not ordinary duplicates and do not
  contribute reclaimable bytes.
- No file is selected for deletion automatically when results first appear.
- Selection helpers are explicit user actions.
- At least one file in every duplicate group must remain unselected.
- Deletion remains permanent and goes through the existing confirmation flow.
- Replacing files with hard links, reflinks, or deduplicating filesystem extents
  is out of scope for the first implementation.

## Scan Pipeline

Use a staged pipeline to minimize disk I/O.

### Stage 1: Enumeration

Collect regular-file candidates:

- absolute path;
- size;
- modified time;
- device and file identity where available;
- hidden state;
- root id;
- accessibility;
- sparse/allocation metadata where available.

Skip:

- directories after traversal;
- device files, sockets, pipes, and other non-regular files;
- inaccessible files;
- symlinks unless Include symlink files is enabled;
- mount boundaries unless explicitly enabled;
- files below the configured minimum size.

Group by logical file size. Groups containing one physical file are discarded.

Zero-byte files:

- are optionally included;
- do not require hashing;
- appear in a separate group because reclaimable size is zero.

### Stage 2: Physical Identity

Before reading content, group aliases by physical identity.

- Linux: device id plus inode.
- Windows: volume serial plus file id from native handle metadata.
- macOS: volume/file identifier.

Paths sharing one physical identity are marked AlreadyLinked.

They may be displayed under the same content group for information, but only
one physical allocation contributes to duplicate count and reclaimable bytes.

### Stage 3: Quick Fingerprint

For remaining same-size candidates, compute a quick fingerprint from:

- file size;
- first fixed-size block;
- middle block for sufficiently large files;
- last fixed-size block.

Use a stable fast hash available in the project or a small dedicated
implementation. The quick fingerprint is only a filter and is never final proof
of equality.

Discard quick-fingerprint groups containing one physical file.

### Stage 4: Full Hash

Compute SHA-256 for remaining candidates using a streaming reader.

Requirements:

- one read produces only the required hash;
- bounded worker count;
- global in-flight byte budget;
- cancellation checks between blocks;
- progress by bytes and files;
- lower I/O priority on Linux using the existing OperationQueue pattern where
  useful;
- no simultaneous random reads from too many files on HDD/USB.

A sensible default is:

- one active hashing reader on rotational or unknown removable media;
- up to two readers on SSD;
- configurable only through an internal policy initially.

### Stage 5: Exact Verification

Before a duplicate group is eligible for cleanup:

- files must have equal size and SHA-256;
- perform byte-for-byte comparison before deletion when files changed during
  the scan window, metadata is ambiguous, or the group is selected for cleanup;
- re-stat files before verification;
- mark changed files stale and remove them from the cleanup selection.

SHA-256 equality may be shown as a confirmed group after normal scan.
Byte verification is the final guard immediately before a destructive plan.

## Shared FileFingerprintService

Add src/core/FileFingerprintService.

Responsibilities:

- quick fingerprint;
- full SHA-256;
- optional byte equality;
- physical file identity;
- cancellation;
- progress callbacks;
- bounded read scheduling;
- optional in-memory cache for the current session.

Suggested API:

- physicalIdentity(path);
- quickFingerprint(path, expectedMetadata, cancelToken);
- sha256(path, expectedMetadata, cancelToken, progress);
- filesEqual(leftPath, rightPath, expectedMetadata, cancelToken, progress).

Cache key must include:

- normalized local path;
- physical identity when available;
- size;
- modified time with platform precision;
- optional change-time/file-id generation where available.

Do not persist hash results across sessions in the first implementation.
A persistent cache can be added later only with robust invalidation.

ChecksumCalculator should eventually delegate its SHA-256 work to this service
instead of keeping a second file-reading implementation. MD5 and SHA-1 remain
property-dialog features and are not used for duplicate identity.

## Architecture

### DuplicateFinderController

Add src/controllers/DuplicateFinderController.

Properties:

- state: Idle, Enumerating, Fingerprinting, Hashing, Verifying, Finished,
  Canceling, Failed, CleanupReady, Cleaning;
- roots;
- options;
- scanned files/folders;
- candidate files;
- hashed files and bytes;
- skipped/inaccessible counts;
- duplicate group count;
- duplicate physical file count;
- potential reclaimable bytes;
- selected cleanup count and bytes;
- progress and current path;
- result model;
- error and diagnostics.

Invokables:

- canScanRoots(paths);
- start(paths, options);
- cancel();
- clear();
- setSelected(row, selected);
- selectByRule(rule);
- clearSelection();
- buildCleanupPlan();
- confirmAndDeleteSelected();
- revealPath(path);
- compareSelectedPair(left, right).

### DuplicateFinderScanner

Add src/core/DuplicateFinderScanner.

Responsibilities:

- native recursive enumeration;
- size grouping;
- physical identity grouping;
- staged fingerprint/hash scheduling;
- progressive result updates;
- skipped diagnostics;
- generation and cancellation handling.

Reuse LinuxFileEnumerator on Linux and native Windows traversal patterns from
FileSearchScanner. Do not force duplicate scanning through FileSearchController;
their result and pipeline semantics differ.

### DuplicateResultsModel

Use a grouped QAbstractItemModel or a flat list with explicit group roles.

Required roles:

- group id;
- group header flag;
- path;
- display path;
- file name;
- size and size text;
- modified time;
- root label;
- physical identity;
- already-linked flag;
- selected-for-cleanup flag;
- stale flag;
- inaccessible flag;
- keep-protected flag;
- verification state;
- reclaimable bytes;
- group file count.

The model must support thousands of result rows without constructing one QML
object per scanned file before it enters a confirmed duplicate group.

## Selection and Safety

Selection helpers:

- select all except newest;
- select all except oldest;
- select all except shortest path;
- prefer a chosen root;
- prefer left panel root;
- prefer right panel root;
- clear group selection;
- invert within group while preserving one keeper.

Rules:

- no initial auto-selection;
- at least one distinct physical file remains per group;
- hard-linked aliases cannot all be counted as separate reclaimable copies;
- directories are never cleanup rows;
- inaccessible/stale files cannot be selected;
- selection rule results are previewed before deletion;
- selected files changing after scan are deselected and marked stale.

Cleanup plan fields:

- path;
- expected size;
- expected modified time;
- expected physical identity;
- group id;
- keeper path;
- verified-equal state;
- estimated reclaimable bytes.

Immediately before deletion:

1. ensure the keeper still exists;
2. re-stat selected file and keeper;
3. byte-verify when required;
4. reject changed or mismatched files;
5. pass surviving paths to WorkspaceController requestDelete;
6. use the existing permanent-delete dialog and OperationQueue.

Never call QFile::remove directly from the duplicate finder.

## UI

Add qml/components/DuplicateFinderDialog.qml.

Entry points:

- command palette: Find duplicate files;
- folder context menu: Find duplicates here;
- two-panel command: Find duplicates in both panel folders;
- optional disk-usage dialog action.

Dialog layout:

- root selector;
- recursive, hidden, mount-boundary, symlink, zero-byte, and minimum-size
  controls;
- phase progress with files and bytes;
- grouped virtualized results;
- group summary and reclaimable size;
- file metadata and root;
- reveal, preview, properties, and checksum actions;
- explicit selection-helper menu;
- selected cleanup summary;
- Review deletion button;
- cancel and close actions.

The result list should make the keeper visible. A group with selected cleanup
files must clearly show which file remains.

## Performance Policy

- discard unique sizes before reading file content;
- bound workers by storage class, not only CPU count;
- avoid hashing the same physical file twice;
- process sequentially per rotational device;
- batch model updates;
- throttle progress signals;
- keep panel and watcher activity responsive;
- do not retain large file buffers after each block;
- report logical reclaimable size separately from allocated size when sparse
  metadata is available.

Optional trace environment:

- FM_DUPLICATE_TRACE=1;
- stage timings;
- files discarded by size/quick hash;
- bytes read;
- worker policy;
- stale files;
- no file content or full sensitive paths unless the existing trace policy
  permits them.

## Implementation Phases

### Phase 1: Enumeration and size grouping

1. Add options, result structs, scanner, controller, and basic model.
2. Implement native local traversal and cancellation.
3. Group by size and emit candidate summaries.
4. Add physical identity detection.
5. Add the initial dialog and root selection.

### Phase 2: Fingerprint and exact groups

1. Add FileFingerprintService.
2. Add quick fingerprint filtering.
3. Add bounded SHA-256 scheduling.
4. Add hard-link handling.
5. Emit confirmed duplicate groups progressively.

### Phase 3: Result workflow

1. Add grouped result UI.
2. Add preview, reveal, properties, and comparison actions.
3. Add selection helpers.
4. enforce one keeper per group.
5. calculate reclaimable bytes accurately.

### Phase 4: Cleanup integration

1. Build immutable cleanup plans.
2. Re-stat and verify selected files.
3. Route deletion through WorkspaceController and OperationQueue.
4. report partial stale/mismatch results.
5. refresh affected panels, tree paths, and duplicate groups after deletion.

### Phase 5: Shared hashing and polish

1. migrate ChecksumCalculator SHA-256 to FileFingerprintService.
2. migrate strict Folder Compare hashing to the shared service.
3. tune worker policy on HDD, SSD, USB, and network-like mounts.
4. add optional session cache.
5. complete large-tree and cancellation verification.

## Duplicate Finder Acceptance Matrix

- files with unique sizes are never hashed.
- equal-size different files are discarded by fingerprint or full hash.
- exact duplicates appear in one group.
- empty files are optional and report zero reclaimable bytes.
- hard links are identified and do not inflate reclaimable space.
- symlink directory loops are not followed.
- mount boundaries are skipped by default.
- cancel stops enumeration and hashing promptly.
- stale results from an old scan cannot enter a new model.
- no cleanup file is selected initially.
- selection helpers always leave a keeper.
- changing a selected duplicate before cleanup blocks its deletion.
- deleting selected duplicates uses the normal permanent-delete confirmation.
- partial access failures remain visible without failing the whole scan.
- scanning large trees does not block panel navigation or scrolling.

# Shared Delivery Rules

## Build and Registration

For each feature:

- add C++ sources to CMakeLists.txt;
- expose controllers through AppServices and QmlEngineBootstrap using the
  existing service ownership pattern;
- add QML files to MY_QML_FILES;
- register required metatypes before queued cross-thread signals;
- keep platform-specific sources behind platform conditions;
- avoid expanding FilePanelController into the owner of feature state.

## State Lifetime

- dialogs may close while work continues only when the controller and product
  behavior explicitly support background operation;
- otherwise closing requests cancellation and waits asynchronously for the
  generation to finish;
- never retain raw QML object pointers in worker code;
- a new request invalidates every prior generation;
- application shutdown cancels and joins active workers without blocking the
  GUI indefinitely.

## Error and Diagnostics Contract

Every scanner/controller reports:

- primary error;
- skipped count;
- inaccessible count;
- bounded detail list;
- current phase;
- current path;
- whether partial results remain valid.

One inaccessible file must not fail a folder comparison or duplicate scan.
Failure of a root itself may fail that side or the complete request depending on
whether useful comparison is still possible.

## Final Release Sequence

1. ship Open With after Windows/Linux application discovery and preference
   behavior pass the acceptance matrix;
2. ship read-only Folder Compare before enabling synchronization if needed;
3. enable Sync only after plan revalidation and destructive gating are complete;
4. ship Duplicate Finder read-only results before enabling cleanup if needed;
5. enable duplicate cleanup only after keeper enforcement and final verification
   are complete.

The read-only stages are valid product checkpoints, but the implementation
should continue to the complete workflows described in this document rather
than remaining permanent analysis-only tools.
