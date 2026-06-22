# MEGA.nz Plugin Implementation Plan

Goal: add a `mega://` provider that is comparable to the current Google Drive plugin from a user-workflow perspective: reading and writing files, creating and deleting folders, renaming, copying between the local filesystem and the cloud, previews, native file-type icons, Pathbar integration, and dedicated support for opening public MEGA links without requiring authentication.

## 1. Scope and Functional Targets

### 1.1. What Google Drive parity means

The MEGA plugin should cover the same classes of operations expected from a cloud file provider in FMQml:

- navigating account roots, folders, the rubbish bin, and published links;
- retrieving metadata: name, size, dates, MIME/suffix, folder flag, read-only flag, and hidden/system flags where applicable;
- downloading files for local operations, Quick Look, previews, and external opening through existing materialization paths;
- uploading local files to MEGA with progress, cancellation, and correct name-conflict handling;
- creating files and folders;
- deleting, moving, and renaming;
- showing used/free account storage;
- caching metadata so the panel does not perform a full network traversal for every small action;
- plugin actions: sign in/out, authorization status, open link, copy MEGA link, and possibly import a public link into the account;
- predictable public-link behavior: links open read-only without authentication, and write/delete operations are unavailable.

### 1.2. Important MEGA-vs-Google Drive difference

MEGA uses client-side encryption, so the provider should not be designed as a thin HTTP wrapper around a REST API. A practical implementation should rely on the official MEGA SDK, or on a small adapter around it, because the plugin needs:

- decrypted names, attributes, previews, and file contents;
- correct handling of public file/folder links and URL keys;
- chunked transfer, retry, resume, and API limit handling;
- account filesystem events;
- public-link generation/import;
- safe storage of session data.

A custom direct HTTP client is acceptable only for a very limited spike/prototype, not as the final architecture.

## 2. Plugin Architecture

### 2.1. Modules

Add a new `src/plugins/mega/` directory with the following main components:

1. `MegaFileProviderPlugin`.
   - Implements `FileProviderPlugin` and `FileActionPlugin`.
   - Registers the `mega` scheme and, if needed, an internal `mega-link` scheme only if distinguishing account paths from public-link paths at registry level becomes necessary.
   - Returns `displayName = "MEGA"` and `pluginId = "mega"`.

2. `MegaFileProvider`.
   - Implements the `FileProvider` interface.
   - Owns `scan`, `entryInfo`, `childPaths`, `copyToLocalFile`, `copyFromLocalFile`, `openRead`, `openWrite`, `createFolder`, `createFile`, `renamePath`, `movePath`, `removePath`, and `storageInfo` behavior.
   - Talks only to the service layer; it must not embed direct SDK logic in UI-facing operations.

3. `MegaSession` / `MegaClient`.
   - Wraps the MEGA SDK: login/logout, session resume, nodes, links, transfers, account storage, and event callbacks.
   - Isolates SDK callbacks from Qt threads and exposes results through a Qt-friendly future/callback API.

4. `MegaPath`.
   - Parses and normalizes paths.
   - Supported formats:
     - `mega:///` — authenticated account root;
     - `mega:///Cloud Drive/...` or another chosen virtual root layout;
     - `mega:///Rubbish Bin/...` if the rubbish bin is included in the tree;
     - `mega://link/<stable-id>/...` for an already opened public link;
     - direct user input such as `https://mega.nz/file/...`, `https://mega.nz/folder/...`, and legacy `#!...` / `#F!...` links, normalized into provider paths.
   - Do not store a public-link secret key in the visible `FileEntry::path` if that path can later be written to logs or history. For public links, prefer a short local `linkId` and keep the original URL/key in encrypted session cache or memory only.

5. `MegaCache`.
   - Caches node handle -> metadata, parent/children, path -> handle, and linkId -> public root mappings.
   - Invalidates on SDK events and after write operations.
   - Keeps public-link cache read-only and scoped to the specific link.

6. `MegaTransferDevice`.
   - Provides a `QIODevice` implementation or a staging-backed adapter for `openRead`/`openWrite`.
   - The first iteration may implement `openRead` through a staging file, provided it is aligned with the cleanup subsystem; later iterations can add a streaming `QIODevice`.
   - For cloud providers, `openWrite` is better implemented first as a temporary local file plus upload-on-close if the current operation expects `QIODevice` semantics.

7. `MegaThumbnailAdapter`.
   - Retrieves built-in preview/thumbnail data from the MEGA SDK for images/videos; if unavailable, passes a materialized local file to the existing `ThumbnailProvider`.
   - Caches results by node handle + fingerprint/mtime.

8. `MegaAuth`.
   - Handles login by email/password or session string, 2FA, and logout.
   - Stores session/token data through the existing secret-storage approach, similar to Google Drive, but with extra care because a MEGA session grants access to decrypted data.

### 2.2. Build and dependencies

Build plan:

1. Add a CMake option named `FM_ENABLE_MEGA_PLUGIN`.
2. Locate the system MEGA SDK through `find_package`, or enable a vendored build only behind a separate flag.
3. Build the plugin as optional: if the SDK is unavailable, the app continues to build without MEGA support.
4. Add two CI configurations:
   - without SDK: verify that the optional plugin does not break the build;
   - with SDK/mock: run adapter, path, and error tests.

## 3. Paths and Pathbar Behavior

### 3.1. Authenticated account

Base user-facing path: `mega:///`.

Possible root layouts:

- minimal: `mega:///` directly shows Cloud Drive contents;
- expanded: `mega:///Cloud Drive`, `mega:///Rubbish Bin`, `mega:///Inbox`, and `mega:///Backups`, if the SDK exposes these roots reliably.

Recommendation: start with the expanded layout only if it does not complicate operations. Otherwise, use Cloud Drive as the root and add the rubbish bin later as a separate action/place.

### 3.2. Public links without authentication

Requirement: the user can paste a MEGA link directly into the Pathbar, and if the link is public, it opens without account sign-in.

Flow:

1. Pathbar receives text.
2. Registry/provider detection recognizes:
   - `https://mega.nz/file/<id>#<key>`;
   - `https://mega.nz/folder/<id>#<key>`;
   - `https://mega.nz/#!<id>!<key>`;
   - `https://mega.nz/#F!<id>!<key>`;
   - optionally `mega.nz/file/...` without a scheme if the Pathbar already supports similar normalization.
3. `MegaPath::fromUserInput()` validates the link and creates an internal path:
   - file link: `mega://link/<linkId>/<filename>` or a virtual single-file directory;
   - folder link: `mega://link/<linkId>/`.
4. `MegaSession` opens the public node/folder through the SDK without login.
5. The provider exposes capabilities as `Browse | ReadMetadata | Transfer`; `Create`, `Rename`, and `Remove` are disabled.
6. For a file link, the Pathbar should allow:
   - opening a single-file view;
   - downloading the file;
   - Quick Look/preview;
   - copying the file into a local panel or, if the user is authenticated, importing it into the account.

### 3.3. Public-link security

- Do not persist the full URL with its key in plain-text recent paths, logs, or telemetry.
- If the app stores Pathbar history, store a display label such as `mega://link/<linkId>/...` without the key, or ask the user.
- Do not show the key in the UI after opening the link.
- Public-link errors should distinguish invalid format, missing key, removed link, quota/bandwidth limit, and network error.

## 4. FileProvider Operations

### 4.1. Capabilities

For an authenticated account:

- `Browse` — yes;
- `ReadMetadata` — yes;
- `Create` — yes;
- `Rename` — yes;
- `Remove` — yes;
- `Transfer` — yes;
- `Watch` — enable only after the SDK event bridge is implemented.

For a public link:

- `Browse` — for folder links;
- `ReadMetadata` — yes;
- `Transfer` — download/copy to local;
- `Create/Rename/Remove` — no;
- `Watch` — no.

### 4.2. `scan` and metadata

`scan(path)` should:

1. normalize the path;
2. resolve the node handle/link root through `MegaCache`;
3. on cache miss, request children from the SDK;
4. convert each node into `FileEntry`:
   - `name`, `path`, `suffix`, `size`, `sizeText`;
   - `modified`, `created`, and date display text;
   - `isDirectory`, `isReadOnly`, `isImage`, `hasThumbnail`;
   - `mimeType` through `QMimeDatabase` by name and/or SDK attributes;
   - `iconName` through the shared resolver or file-type asset names;
   - `providerCapabilitiesText` for read-only/public/quota states.
5. emit batches rather than one huge list for large folders;
6. finish with `finished(path, success, generation, error)`.

### 4.3. Downloading

`copyToLocalFile(sourcePath, destinationFilePath, progress, error)`:

- use SDK download transfers;
- write to a `.part` file next to the destination and atomically rename after success, unless the upper layer already owns this behavior;
- propagate progress and cancellation;
- map quota, bandwidth, auth, not-found, and permission-denied errors;
- do not leave partial files behind after cancellation or error.

`openRead(path, stagingParentPath)`:

- for small files, downloading to staging and returning a `QFile` is acceptable;
- for large files, prefer a streaming/range adapter, but that can be a later iteration;
- staging must be registered with the cleanup subsystem once it is centralized.

### 4.4. Uploading

`copyFromLocalFile(sourceFilePath, destinationPath, progress, error)`:

- interpret `destinationPath` according to the current operation contract: either as the full future path or as parent+name;
- verify that the parent exists and is writable;
- use an SDK upload transfer;
- update `MegaCache` after successful upload;
- support overwrite/rename-on-conflict according to the existing FMQml policy;
- show a clear error on quota exceeded.

`copyFromLocalFiles(items, progress, error)`:

- implement after single-file upload;
- group uploads by parent folder;
- limit concurrency so the plugin does not hit SDK/API constraints or make the UI noisy;
- return the current file through the progress callback.

`openWrite(path, truncate)`:

- MVP: use a staging file that uploads to MEGA on close;
- explicitly define where upload-on-close errors surface so the upper layer does not treat the operation as successful too early;
- if the current `openWrite` contract cannot express an async commit, temporarily avoid advertising workflows that require direct writes through `QIODevice` and rely on `copyFromLocalFile` instead.

### 4.5. Create, delete, rename, and move

- `createFolder(parentPath, name)` — SDK create folder, then cache insert.
- `createFile(parentPath, name)` — create an empty local temp file and upload it, or use an SDK create-empty-file operation if available.
- `renamePath(oldPath, newName)` — SDK rename, then update cache paths for the subtree.
- `movePath(sourcePath, destinationPath)` — SDK move node inside the account; for public links, return false/read-only.
- `removePath(path)` — decide UX semantics: move to Rubbish Bin by default, and expose hard delete only as a separate action or when the MEGA SDK treats remove that way. For Google Drive parity, prefer trash-like behavior first if the SDK allows it.

## 5. Previews and Native Icons

### 5.1. Icons

MEGA should not introduce a separate file-type icon system. Rules:

- folders use the shared folder icon;
- files use `FileTypeIconResolver` by suffix/MIME;
- MEGA-specific entities get separate overlays/assets only when needed: public-link root, rubbish bin, shared folder;
- `FileEntry::iconName` should be populated with the same values the UI expects for local/GDrive files.

### 5.2. Thumbnail pipeline

Priorities:

1. If the SDK returns a thumbnail/preview attribute, use it without downloading the full file.
2. If no thumbnail is available but the file is small and supported, materialize it into a temporary file and pass it to the existing thumbnail pipeline.
3. For large videos/archives, do not download the whole file just to produce a thumbnail unless an explicit limit allows it.
4. Cache thumbnails separately from metadata with a key such as `nodeHandle + mtime/fingerprint + requestedSize`.
5. For public links, apply the same rules but never include the link key in the cache filename.

### 5.3. Quick Look

- For images and documents, Quick Look should work through `openRead`/materialization.
- For video/audio, the MVP may download to staging before playback, but the planned shape should include streaming/range support.
- The UI should show materialization progress for large files.

## 6. Authorization and Plugin Actions

### 6.1. Actions

Minimum `FileActionPlugin` actions:

- `mega.signIn` — open the sign-in dialog;
- `mega.signOut` — sign out and clear the session;
- `mega.authStatus` — show email/account type/quota;
- `mega.openLink` — open a MEGA link from clipboard/manual input;
- `mega.copyLink` — create/copy a public link for the selected file if the user is authenticated and the SDK allows it;
- `mega.importLink` — import an opened public link into the account if the user is authenticated.

### 6.2. MFA and sign-in errors

- The first stage may support email/password plus session resume.
- If the account requires 2FA, the action returns a `requiresMfa` state and the UI shows the second step.
- Errors must be human-readable: invalid credentials, MFA required, network unavailable, account blocked, SDK initialization failed.

### 6.3. Secret storage

- Do not store the password.
- Store only session/token data, preferably through platform secure storage.
- Logout removes the session, clears metadata cache, and closes active public-link sessions if they were attached to the account.

## 7. UI Integration

1. Add MEGA to the places/plugin list if a places plugin or registry UI mechanism exists.
2. Pathbar should route `https://mega.nz/...` through provider normalization instead of trying to open it as a normal web URL.
3. Show read-only status for public-link roots.
4. Show progress for long operations in the existing operations panel.
5. Context menu:
   - Download/copy to local;
   - Copy MEGA link;
   - Import to my MEGA;
   - Sign in/out/status;
   - Remove/Rename/Create only when capabilities allow them.
6. Show clear errors for quota/bandwidth limits and do not leave operations pending forever.

## 8. Testing

### 8.1. Unit tests without real MEGA

- `MegaPathTest`:
  - `mega:///` normalization;
  - file/folder public-link parsing;
  - legacy hash links;
  - protection against logging the key;
  - parent/child/fileName behavior.
- `MegaCacheTest`:
  - children cache;
  - rename/move invalidation;
  - public-link scope isolation.
- `MegaErrorMappingTest`:
  - SDK/API error -> user message;
  - auth/quota/not-found/permission/network.
- `MegaFileEntryMappingTest`:
  - suffix/MIME/icon/isImage/hasThumbnail/readOnly.

### 8.2. Integration tests with a fake SDK

Create a fake `MegaClient` for:

- account tree;
- public folder link;
- public file link;
- transfer progress/cancel/error;
- quota exceeded;
- auth expired;
- metadata events.

Verify:

- scanning large folders emits batches;
- download creates the correct local file;
- upload updates cache;
- remove/rename/move are reflected in `childPaths`;
- public links are read-only;
- cancellation does not leave `.part` files behind.

### 8.3. Manual E2E scenarios

1. Sign in to MEGA, open `mega:///`, and navigate through several folders.
2. Copy a local file to MEGA, wait for progress to complete, and refresh the folder.
3. Copy a file from MEGA to a local disk.
4. Create a folder, rename it, and delete it.
5. Open a public folder link by pasting it into the Pathbar without signing in.
6. Open a public file link by pasting it into the Pathbar without signing in, then download the file.
7. Check image previews and fallback behavior for files without thumbnails.
8. Verify that native icons match local files with the same extensions.
9. Verify logout: `mega:///` no longer opens private data.
10. Verify bandwidth/quota/auth errors.

## 9. Implementation Phases

### Phase 0. Discovery and SDK spike

- Build the MEGA SDK in a local development configuration.
- Create a console spike: login, list root, download public file, list public folder.
- Record the CMake strategy and SDK threading constraints.
- Decide whether a streaming `QIODevice` can be implemented safely immediately, or whether the MVP needs staging.

Output: a short technical note and a minimal mockable `MegaClient` interface.

### Phase 1. Plugin skeleton and paths

- Add `src/plugins/mega`.
- Implement `MegaFileProviderPlugin`, `MegaPath`, and `MegaCache` skeletons.
- Add the optional CMake target.
- Add `MegaPathTest` unit tests.
- Make Pathbar recognize `mega://` and `https://mega.nz/...`.

Output: the plugin loads but may return a clear `MEGA SDK unavailable/not signed in` error.

### Phase 2. Read-only public links

- Open a public folder link without authentication.
- Open a public file link as a single-file container or direct entry.
- Implement `scan`, `entryInfo`, `copyToLocalFile`, and `openRead` through staging.
- Expose read-only capabilities.
- Add thumbnail MVP for public image links.

Output: the requirement to paste and open MEGA links from the Pathbar is satisfied in read-only mode.

### Phase 3. Account authorization and account reads

- Login/session resume/logout/status.
- `mega:///` scan for Cloud Drive.
- Metadata cache.
- Storage quota.
- Read/download/openRead for account files.

Output: private account data can be browsed and downloaded.

### Phase 4. Account writes and mutations

- Upload local -> MEGA.
- Create folder/file.
- Rename/move/remove.
- Conflict policy.
- Batch upload with limited concurrency.
- Cache invalidation after mutations.

Output: core file operations are comparable to Google Drive.

### Phase 5. Previews, icons, and polish

- SDK thumbnails/previews.
- Thumbnail cache.
- Icons/overlays for MEGA-specific roots.
- Quick Look UX for large files.
- `copyLink` and `importLink` actions.

Output: cloud UX looks native in the file panel.

### Phase 6. Watch/events and reliability

- SDK event bridge -> cache invalidation/rescan.
- Retry/backoff for transfers.
- Resume interrupted transfers if the SDK and operation contract allow it.
- Diagnostics and debug logging without secrets.

Output: the provider is resilient to external changes and network failures.

## 10. Risks and Mitigations

| Risk | Impact | Mitigation |
| --- | --- | --- |
| MEGA SDK is hard to build | Plugin breaks portable builds | Optional CMake, feature flag, CI without SDK |
| Client-side encryption | A quick REST-only implementation is not viable | Use the SDK as the required backend |
| Public-link key leaks into history/logs | Access to files can leak | Internal `linkId`, log redaction, no key in `FileEntry::path` |
| `openWrite` async commit does not match the `QIODevice` contract | False successful writes | For MVP, rely on `copyFromLocalFile`; limit direct `openWrite` |
| Large thumbnails/Quick Look downloads gigabytes | Poor UX and bandwidth waste | SDK preview first, size limits, explicit progress |
| MEGA quota/bandwidth limits | Operations fail unclearly | Error mapping and user-visible messages |
| SDK callbacks arrive off the UI thread | Race/crash | Single Qt bridge layer, queued connections, generation checks |
| Move/delete semantics differ from GDrive | Data loss | Prefer trash-like remove first; expose hard delete only as a separate action |

## 11. MVP Definition of Done

The MVP is ready when all of the following are true:

- `mega:///` opens the authenticated account after login/session resume;
- a public `https://mega.nz/folder/...` link opens from the Pathbar without authentication;
- a public `https://mega.nz/file/...` link opens from the Pathbar without authentication and the file can be downloaded;
- a file can be downloaded from the account to a local disk;
- a local file can be uploaded into the account;
- a folder can be created, renamed, and deleted in the account;
- `FileEntry` correctly populates size, dates, MIME/suffix, directory/read-only flags;
- images get thumbnails either through SDK preview or through the existing fallback;
- file icons match local files of the corresponding types;
- auth/quota/not-found/network errors are visible to the user;
- public-link secrets and sessions are not written to logs;
- there are unit tests for paths and error mapping, plus fake-client integration tests for transfer/scan.
