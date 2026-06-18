# Gradient Visual Refresh Follow-Up

This document records the next pass after the first gradient integration.

## Current direction

The first iteration established the main gradient system:

- `useGradientColors` gates gradient chrome.
- `AmbientPanelBackground` provides a shared ambient surface.
- Main workspace underlay, file panels, sidebar, preview pane, toolbar,
  Favorites, and This PC already use the shared visual language.
- Built-in theme gradient intensity has been tuned enough for the first pass.

The next iteration should preserve the visual richness while improving
consistency and readability. Dense file-management areas should remain quiet.

## Next candidates

1. Dialog shells
   - File: `qml/components/dialogs/DialogShell.qml`.
   - This would affect Settings, Theme Editor, Properties, Disk Usage, Batch
     Rename, Checksum, Plugin Manager, Steam Proton, Help, and similar dialogs.
   - Add `AmbientPanelBackground` at shell level with low strength, around
     `0.45-0.55`.
   - Avoid adding separate gradients to every section or row.

2. Command palette
   - File: `qml/components/CommandPalette.qml`.
   - It still has its own local gradient.
   - Replace the shell background with `AmbientPanelBackground`, likely using
     `baseColor: Theme.panelSurfaceStrong` and strength around `0.55-0.65`.
   - Keep command rows flat and readable.

3. File panel footer and status chrome
   - Files:
     - `qml/components/filepanel/FilePanelFooter.qml`
     - `qml/components/filepanel/FilePanelStatusBar.qml`
     - `qml/components/filepanel/FilePanelStatusRail.qml`
     - `qml/components/filepanel/FilePanelSelectionActions.qml`
   - These currently read as flat `panelSurfaceStrong` bands.
   - Apply a very subtle chrome-gradient so the lower panel chrome matches the
     rest of the app without competing with file rows.

4. Operations drawer
   - File: `qml/components/OperationsDrawer.qml`.
   - Apply ambient treatment to the drawer shell only.
   - Individual operation rows should stay mostly flat for scanability.

5. Path bar and toolbar input islands
   - Files:
     - `qml/components/PathBar.qml`
     - `qml/components/toolbar/ToolbarPathEditor.qml`
     - `qml/components/toolbar/ToolbarSearch.qml`
   - Use a light control-surface wash for focused and active states.
   - Do not reduce address/search text contrast.

6. Theme selector and theme editor preview
   - Files:
     - `qml/components/ThemeSelectorMenu.qml`
     - `qml/components/ThemeEditorPreviewCard.qml`
   - Sync previews with the real ambient strengths used by underlay, panel,
     toolbar, and dialog surfaces.
   - This is important now that gradients are no longer one global intensity.

7. Properties dialog content panels
   - File: `qml/components/PropertiesDialog.qml`.
   - Start with the main shell and perhaps the hero/top section.
   - Avoid applying gradients to every property row.

8. Disk Usage dialog
   - File: `qml/components/DiskUsageDialog.qml`.
   - Good candidate for gradient shell and selected high-level cards.
   - Charts, tables, and dense rows should remain flat.

9. Quick Look
   - File: `qml/components/QuickLook.qml`.
   - If it opens as a standalone preview overlay, align it with the same
     dialog/surface language so it does not feel visually separate.

## Areas to avoid

Do not apply gradients broadly to:

- file delegates: `FileTableDelegate`, `FileBriefDelegate`, `FileDelegate`;
- context menus: `ThemedContextMenu`, `ThemedMenuItem`, `ColumnPickerMenu`;
- dense dialog rows and repeated list rows;
- preview content areas for text, images, PDF, audio, video, and rendered file
  content.

These areas prioritize readability and accurate content inspection.

## Suggested order

1. `DialogShell`
2. `CommandPalette`
3. File panel footer/status chrome
4. `OperationsDrawer`
5. Theme selector/editor preview sync
6. Path/search focused control wash

## Verification

- Check at least one dark theme and both light themes.
- Pay special attention to Porcelain Bloom and Catppuccin Latte.
- Verify `useGradientColors=false` still produces coherent flat UI.
- Run:
  - `cmake --build build --target fm`
  - `ctest --test-dir build --output-on-failure`
  - `git diff --check`
  - `rg -n "gradient:|GradientStop|#[0-9A-Fa-f]" qml`
