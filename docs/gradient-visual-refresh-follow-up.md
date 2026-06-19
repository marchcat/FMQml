# Gradient Visual Refresh Status

This document records the state after the gradient visual refresh passes.

## Current state

The gradient system is functionally closed.

- `useGradientColors` gates gradient chrome and remains available in Settings.
- `AmbientPanelBackground` is the shared ambient surface for app chrome.
- Main workspace underlay, file panels, sidebar, preview pane, toolbar,
  Favorites, This PC, dialog shells, command palette, lower file-panel chrome,
  operations drawer, and toolbar path/search focus states use the shared visual
  language.
- Theme selector and Theme Editor preview now account for the real ambient
  strengths used by app underlay, panels, dialogs, and lower chrome.
- Built-in theme gradient intensity has been tuned enough for the current pass.

The design direction is restrained: gradients add depth to top-level chrome,
while dense file-management areas stay quiet and readable.

## Implemented surface strengths

Current representative strengths:

- app/window underlay: around `0.95`;
- workspace and panel-level surfaces: around `0.68-0.74`;
- toolbar/sidebar/preview surfaces: around `0.62-0.70`;
- command palette shell: around `0.60`;
- dialog shells and operations drawer card: around `0.50`;
- operations drawer compact chip: around `0.46`;
- file-panel footer and selection actions: around `0.28`;
- file-panel status rail/bar: around `0.34`.

These are visual tuning values, not a public API. Prefer matching nearby
surfaces over adding new one-off strengths.

## Areas intentionally kept flat

Do not apply gradients broadly to:

- file delegates: `FileTableDelegate`, `FileBriefDelegate`, `FileDelegate`;
- context menus: `ThemedContextMenu`, `ThemedMenuItem`, `ColumnPickerMenu`;
- dense dialog rows and repeated list rows;
- preview content areas for text, images, PDF, audio, video, and rendered file
  content;
- operation rows inside `OperationsDrawer`.

These areas prioritize scanability and accurate content inspection.

## Remaining polish only

Do not continue adding gradients speculatively. Future work should be treated as
targeted polish or bug fixing only.

Possible candidates if they visibly stand out:

- `PropertiesDialog.qml`: only the hero/top section, not every property row.
- `DiskUsageDialog.qml`: only high-level cards, not charts, tables, or dense
  rows.
- `QuickLook.qml`: only if it looks visually disconnected from dialog chrome.
- `PathBar.qml`: only if the breadcrumb surface reads noticeably flatter than
  the toolbar path/search controls.

## Verification

For future changes in this area:

- Check at least one dark theme and both light themes.
- Pay special attention to Porcelain Bloom and Catppuccin Latte.
- Verify `useGradientColors=false` still produces coherent flat UI.
- Run:
  - `cmake --build build --target fm`
  - `ctest --test-dir build --output-on-failure`
  - `git diff --check`
  - `rg -n "gradient:|GradientStop|#[0-9A-Fa-f]" qml`

When auditing `rg` output, expect existing hits in SVG assets, splash, and theme
preview components. New feature QML should not introduce raw component-local
colors.
