---
name: OS999 Development Observatory
description: Dark technical dashboard for parity and evidence, with restrained live-state glow.
colors:
  bg: "#0a1019"
  panel: "#101b29"
  raised: "#152435"
  line: "#294055"
  text: "#eaf2f8"
  muted: "#a2b7c8"
  cyan: "#67d5f4"
  verified: "#69e3b4"
  implemented: "#6cb7ff"
  in_progress: "#e5c476"
  blocked: "#ff929c"
  pending: "#687d94"
typography:
  display:
    fontFamily: "IBM Plex Sans, sans-serif"
    fontSize: "36px"
    fontWeight: 500
    lineHeight: 1.15
    letterSpacing: "-.025em"
  headline:
    fontFamily: "IBM Plex Sans, sans-serif"
    fontSize: "18px"
    fontWeight: 500
  title:
    fontFamily: "IBM Plex Sans, sans-serif"
    fontSize: "16px"
    fontWeight: 500
  body:
    fontFamily: "IBM Plex Sans, sans-serif"
    fontSize: "15px"
    lineHeight: 1.5
  label:
    fontFamily: "IBM Plex Sans, sans-serif"
    fontSize: "11px"
    fontWeight: 600
    letterSpacing: ".06em"
  mono:
    fontFamily: "ui-monospace, monospace"
rounded:
  badge: "4px"
  control: "6px"
  alert: "10px"
  panel: "14px"
spacing:
  tight: "8px"
  compact: "12px"
  standard: "16px"
  panel: "23px"
  section: "26px"
components:
  button-quiet:
    backgroundColor: "transparent"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "6px 11px"
  button-quiet-hover:
    backgroundColor: "{colors.raised}"
  input-search:
    backgroundColor: "{colors.bg}"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "8px"
  panel:
    backgroundColor: "{colors.panel}"
    rounded: "{rounded.panel}"
    padding: "{spacing.panel}"
  badge:
    textColor: "{colors.cyan}"
    rounded: "{rounded.badge}"
    padding: "5px 8px"
---

# Design System: OS999 Development Observatory

## Overview

**Creative North Star: "Development Observatory"**

Blue-black instrument panels make evidence the center of attention. Cool white
type carries the dense reading; cyan identifies navigation and live monitoring.
The user confirmed a professional dark technical aesthetic with subtle glow.
Controls remain compact, explicit and close to the evidence they reveal.

This document covers only the Python-served browser dashboard, not the classic
desktop. It records `assets/dashboard/index.html`, `dashboard.css` and
`dashboard.js` as inspected on 7 November 2026. The dashboard surface brief owns
page composition. Tokens here describe implementation, not rendered acceptance:
the CUA host was unavailable, so screenshots and visual acceptance remain pending.

**Key Characteristics:**

- Dark tonal surfaces with fine ruled boundaries.
- Restrained live-state glow and a moving monitoring line.
- Labeled acceptance states and evidence-first charts.
- Compact typography with monospace revision identifiers.

## Colors

The palette combines cool, low-light surfaces with legible status accents.

### Primary

- **Instrument Cyan** (`cyan`): links, focus outlines, active navigation and the
  moving timeline stroke. It identifies an action or monitoring state.

### Secondary

- **Accepted Mint** (`verified`): accepted gates and the live connection lamp.
- **Implementation Blue** (`implemented`): implementation evidence awaiting acceptance.
- **Uncertainty Amber** (`in_progress`): work in progress and warning notices.
- **Failure Coral** (`blocked`): blockers, failures and connection errors.
- **Pending Slate** (`pending`): unaccepted work without stronger status evidence.

### Neutral

- **Night Base** (`bg`): page canvas and recessed fields.
- **Instrument Panel** (`panel`): chart and evidence containers.
- **Raised Slate** (`raised`): navigation and quiet-button hover surfaces.
- **Rule Slate** (`line`): separators, borders and empty progress tracks.
- **Cool White** (`text`): primary reading and values.
- **Muted Steel** (`muted`): explanations, labels and secondary evidence.

**The Evidence Color Rule.** Acceptance and implementation keep separate colors
and explicit labels; color alone never establishes acceptance.

## Typography

**Display Font:** IBM Plex Sans with sans-serif fallback.
**Body Font:** IBM Plex Sans with sans-serif fallback.
**Label/Mono Font:** ui-monospace with monospace fallback for identifiers.

Medium-weight headings provide hierarchy without oversized promotional type.
Use the frontmatter hierarchy for the main heading, section heading, title,
body and badge. Small supporting copy is generally 11–13px; checklist summaries
are 13px. Monospace revisions and count treatments use tabular numerals.
Main headings reduce to 30px at the intermediate breakpoint and 28px on phones.
The stylesheet specifies the font family; this documentation does not certify
font loading or the final rendered metrics.

## Layout

Desktop uses a 76px top bar and a 292px sticky checklist/navigation rail.
Main content has 35px 38px 40px padding and a 1640px maximum width.
The parity panel pairs a 245px acceptance column with component status buttons;
the actual markup places the status legend and latest-build strip below it.
The milestone timeline scrolls horizontally with an 890px minimum track.

At 1600px and above, component rows use three columns and main padding expands.
At 1150px and below, the rail narrows to 250px and alerts stack. At 820px and
below, the rail becomes a normal-flow block above the page with horizontal
navigation and a checklist capped at 220px. At 560px and below, the parity
panel stacks and panel padding reduces to 18px. Tables and wide charts scroll.

The final stylesheet overrides the earlier grid treemap with two proportional
flex rows, a 420px total height and square groups; phone height is 520px.
Do not document the superseded grid declarations as current behavior.

## Elevation & Depth

Tonal layering and thin borders establish depth. Cards do not cast shadows.
Only the accepted live connection lamp has a diffuse mint glow
(`0 0 9px #69e3b455`). Focus is a cyan outline (2px, offset 4px), not a glow.

**The Quiet Instrument Rule.** Reserve glow for the live connection lamp;
use tonal changes and borders to distinguish content and interaction.

The timeline dash moves linearly over 12 seconds. It means monitoring is live,
not that delivery is accelerating. Reduced-motion preferences disable animation
and transitions. No general hover-transition duration is currently defined.

## Shapes

Panels have the largest soft corners; fields and quiet buttons use compact
curves. Badges are nearly square. Component rows are flat ruled strips, and
the final treemap groups are square adjoining rectangles. Small state swatches
use 2px corners; timeline nodes and the connection lamp are circular.

## Components

### Buttons

Quiet outlined buttons provide Refresh and Clear filters. Their transparent
surface becomes Raised Slate on hover. Component rows are full-width buttons
with a small donut, a title and explicit accepted/implemented counts; selecting
one filters the checklist. Shared focus-visible outlines apply to both.

### Chips

The Local development badge is informational, with cyan lettering and a thin
blue border. The status legend is a group of text buttons with small colored
swatches and counts; each filters the checklist by state.

### Cards / Containers

Panels use Instrument Panel, Rule Slate borders and the panel spacing token.
Alerts use a slightly distinct dark surface, 16px padding, a status icon and
supporting text. Build records remain ruled rows rather than floating tiles.

### Inputs / Fields

Search and selects use recessed Night Base, cool text and a thin Rule Slate
border. Search has a cyan caret. Controls retain visible labels or accessible
names; keyboard focus uses the shared outline. No disabled-field or validation
error variant is currently implemented.

### Navigation

Six text links expose hash-routed views. Active links use cyan text over a
blue-slate fill (`#1c3549`) and `aria-current`. Hover uses Raised Slate.
On narrow screens these links form a horizontally scrolling row.

### Evidence charts

Donuts show accepted counts alongside other status slices; acceptance remains
explicit in SVG accessible labels. Task details use native disclosure widgets
with status text and source links. Compatibility separates unmeasured apps into
an Unknown lane and supplies a table. Missing benchmarks show dashed empty
tracks labeled Not measured. Treemap area represents tracked behaviors, not
file sizes. Dependency Gantt dates for undated work are illustrative slots;
the accompanying text and table retain that distinction.

## Do's and Don'ts

### Do:

- **Do** pair status color with a readable label or explicit count.
- **Do** preserve unknown measurements and source links in every evidence view.
- **Do** retain visible keyboard focus and reduced-motion behavior.
- **Do** review the rendered desktop and phone layouts before visual acceptance.

### Don't:

- **Don't** equate implementation blue with accepted mint.
- **Don't** turn missing measurements into zero-valued benchmark bars.
- **Don't** interpret timeline motion as delivery velocity.
- **Don't** claim screenshots or rendered validation from source inspection.
