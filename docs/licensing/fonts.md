# Meshbus font selection

U8g2 and its fonts are maintained in the external west project
[`sdk-u8g2`](https://github.com/Meshbus/sdk-u8g2). Its
[complete notices, font catalog and source records](https://github.com/Meshbus/sdk-u8g2/blob/main/fonts/README.md)
travel with that repository. The Meshbus manifest pins the source revision.
The inventory contains 1,616 arrays with per-font license and review status.

From the west workspace, validate that project's inventory with
`python3 modules/lib/u8g2/scripts/font_inventory.py --check`.

### Fonts selected by Desktop

The ZUI defaults (`modules/lib/zui/Kconfig` in the workspace) select four arrays.
Desktop labels are English.
The drawing implementation (`modules/lib/zui/src/zui_draw.c`) maps these to the UI roles
shown below. This is source configuration evidence, not an inspection
of every released firmware image. Firmware packaging reads defined font
array object symbols from each final unstripped ELF and includes their
attributions, family notices and selected supplemental terms in NOTICE.txt.
Private CI `material-evidence/licenses/u8g2/fonts/selected-fonts.json` retains
catalog status and license;
collection does not approve restricted or review-required selections.

| Array / role | Recorded permission | Relevant conditions and retained evidence |
| --- | --- | --- |
| `u8g2_font_helvB08_tr` / primary text | Adobe/DEC X11 font permission | Use, modification, distribution and sale are permitted with copyright and permission notices; no use of the authors' names for publicity without permission. [Notice](https://github.com/Meshbus/sdk-u8g2/blob/main/fonts/notices/u8g2-upstream-fntgrpadobex11-pre.txt). |
| `u8g2_font_haxrcorp4089_tr` / secondary text | CC BY-SA 3.0 | Attribution and the applicable share-alike conditions apply to font adaptations. This is not CC0 or a noncommercial license. [Original designer page](https://fontstruct.com/fontstructions/show/192981/haxrcorp_4089), [upstream attribution](https://github.com/Meshbus/sdk-u8g2/blob/main/fonts/notices/u8g2-upstream-fntgrpfontstruct-pre.txt), [license](https://github.com/Meshbus/sdk-u8g2/blob/main/fonts/notices/u8g2-texts-cc-by-sa-3-0-txt.txt). |
| `u8g2_font_profont11_mr` / keyboard | MIT | Preserve copyright and permission notice. [ProFont notice](https://github.com/Meshbus/sdk-u8g2/blob/main/fonts/notices/u8g2-upstream-fntgrpprofont-pre.txt). |
| `u8g2_font_profont22_tn` / large numbers | MIT | Same ProFont notice. |

None of these four fonts has a noncommercial-only restriction in the recorded
permissions. Permission to use a font without a fee does not remove attribution,
source, modification, or redistribution conditions.

### Meshbus font selection policy

Meshbus excludes WenQuanYi and Unifont arrays from its font selections,
including non-Chinese Unifont subsets. Desktop uses the configured fonts without
a Chinese font fallback. Generic UTF-8 drawing is available for independently
reviewed fonts. Additional Meshbus font selections must satisfy
the [compiled dependency policy](../../LICENSING.md#compiled-dependency-admission).

Both excluded families have GPL-2.0-or-later terms with font embedding
exceptions. These exclusions are a deliberate SDK dependency choice, not a
claim that every GPL-2.0-or-later font is GPL-3.0-only. The font exceptions did
not establish a blanket exception for linking fonts into closed firmware.

The external source inventory contains 15 restricted arrays and 87 arrays
requiring further review. Their presence does not make them approved Meshbus
product dependencies; consult the module's
[restrictions and unresolved items](https://github.com/Meshbus/sdk-u8g2/blob/main/fonts/README.md#restrictions-and-unresolved-items)
when selecting additional fonts.

## Website display font

The website self-hosts Silkscreen regular Latin from `@fontsource/silkscreen`
5.3.0 under OFL-1.1. The complete upstream copyright/license is retained in the
website build notices. See [website provenance](../../web/README.md#artwork-and-font-provenance).
This display font is separate from firmware font selections.
