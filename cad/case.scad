// ============================================================
// Claude Monitor enclosure — LCDWIKI E32R32P (3.2" ESP32-32E CYD)
// + Akyga LP405090 LiPo battery
//
// Two printable parts, selected by the PART variable at the
// bottom of this file: "body" and "lid". Render/export each
// separately (F6 -> Export as STL in OpenSCAD, or the CLI calls
// used to generate cad/stl/*.stl).
//
// Board dimensions are from LCDWIKI's
// E32R32P_E32N32P_Specification_V1.0.pdf, sections 3.2/3.4/4.1/5.1.
// Battery dimensions are user-measured (Akyga LP405090).
// Everything is a variable — re-measure your actual hardware and
// tweak the numbers below before printing if anything doesn't
// match.
// ============================================================

/* [Board — datasheet sec 5.1 / 3.4, landscape orientation (long
   edge horizontal), matching this project's default rotation 3.
   Hole spacing cross-checked against LCDWIKI's E32N32P_Size.pdf
   (same PCB, no-touch sibling SKU, clearer print of the same
   drawing) — that copy states the short-axis hole spacing in
   plain text as "48.00(HOLE)", not the 54.64 originally used here
   (54.64 was actually "(RTP OD)", a touch-panel outline dimension
   misread off the busier E32R32P sheet). ] */
pcb_l        = 93.70;  // PCB long-edge length          (sec 5.1 "93.70(PCB)")
pcb_w        = 55.04;  // PCB short-edge width           (sec 5.1 "55.04(PCB)")
pcb_corner_r = 3.50;   // PCB corner radius              (sec 5.1 "4-R3.50")
// hole_l/hole_w/hole_d: reference only, not used by any geometry below — this
// design has no corner standoffs/screws (see the note near case_body() for
// why), so these just document the real hardware for whoever revisits this.
hole_l       = 86.70;  // mounting-hole pattern, long axis  (sec 5.1 "86.70(HOLE)")
hole_w       = 48.00;  // mounting-hole pattern, short axis (E32N32P_Size.pdf "48.00(HOLE)")
hole_d       = 3.50;   // mounting hole diameter          (sec 5.1 "4-Ø3.20")
stack_d      = 5.70;   // PCB-back-to-glass-front stack depth (sec 3.4 "Module...5.70(D)")
pcb_thick    = 1.60;   // bare PCB thickness (stack breakdown 1.60+1.20+2.40+0.50=5.70)
back_comp_h  = 2.00;   // tallest back-side SMD component height (sec 5.1 side view "5.09 Max(SMD)")

/* [Screen window — sec 3.2/5.1]
   Sized to the LCD BACKLIGHT/glass MODULE footprint (77.70 x 55.04, per
   E32N32P_Size.pdf) on BOTH axes, not the smaller active-pixel area
   (64.80 x 48.60). The module is centered on the PCB in the long axis
   (93.70-77.70=16.00, i.e. exactly 8.00mm margin both ends — matches that
   drawing's separately-printed "8.00" label); in the short axis the module
   (55.04) is exactly pcb_w — flush edge-to-edge, zero margin on the PCB
   itself. This still leaves a real bezel because the front wall is solid
   out to the case's OUTER edge (out_w), not just the cavity edge (in_w) —
   with screen_reveal_extra=0.2 the window ends up ~2.15mm inside out_w,
   plenty for a clean frame. Either way, the smaller active area nests
   entirely inside the module footprint on both axes, so every active pixel
   is still fully exposed. The active area itself is NOT centered within the
   module on the long axis (11.50mm from one end, 17.40mm from the other,
   because of the FPC tail) — but that only matters if you want the window
   to hug the pixels tightly; using the module size instead sidesteps that
   asymmetry entirely. */
screen_w            = 76.75; // LCD BL (glass module), long axis  ("77.70±0.2(LCD BL)")
screen_h            = 55.04; // LCD BL (glass module), short axis — equals pcb_w exactly
screen_r_margin     = 8.00;  // module offset from PCB edge, symmetric both ends
screen_reveal_extra = 0.2;   // extra reveal so the bezel doesn't clip the glass edge

// IMPORTANT: with the corrected hole_w=48.00, the mounting-hole centers
// (±24.00mm) fall *inside* the screen active area's own half-height
// (±24.30mm) — the datasheet's own numbers give the screw holes essentially
// zero (or negative, once ±0.2 tolerance is considered) clearance from the
// visible glass. There is no boss diameter, however small, that avoids the
// window at that exact position — see standoffs() below for how this is
// actually handled (short answer: it isn't a mounting point on this design).

/* [USB-C cutout — sec 4.1 interface photo: Type-C sits on the
   LEFT short edge, vertically centered (derived from sec 5.1's
   13.92/13.60/13.92 left-edge spacing, which sums symmetrically
   around pcb_w/2). The connector is mounted on the PCB's BACK
   (component) side, i.e. it only exists at z >= z_pcb_back and
   extends further in (larger z) from there — usbc_z0 must be >= 0.
   A previous value of -1 put the cutout 1mm in front of z_pcb_back,
   inside the LCD-module/PCB stack itself, which is too close to the
   screen side. Height is still an estimate — verify against your
   physical board and adjust if it's off. ] */
usbc_w  = 11.0;
usbc_h  = 6.0;
usbc_z0 = -2;    // offset added to z_pcb_back (see derived section) — must be >= 0

/* [Battery — Akyga LP405090, user-measured] */
batt_l = 94;
batt_w = 50;
batt_t = 3.0;

/* [Fit & print tuning] */
clearance    = 0.35; // per-side gap around the PCB inside its cavity (XY footprint only —
                      // see batt_z_slack below for the battery's own, separate tolerance)
wall         = 2.0;  // structural wall thickness
front_wall   = 1.5;  // front bezel thickness (thinner: it's mostly cut away by the screen window)
skirt_h      = 2.1;  // lid skirt engagement depth into the case body. A 2mm overall depth cut
                      // was needed; the first attempt took the whole 2mm out of skirt_h alone
                      // (3.5->1.5), which is the wrong place — it's the one thing that actually
                      // holds the lid on. Rebalanced: most of the cut now comes out of slack
                      // that wasn't doing much (see the shrunk comp-to-battery gap and
                      // batt_z_slack below, plus the final case_h margin), so skirt_h only
                      // drops to 2.1 instead of 1.5 — tab thickness (skirt_h*0.5) is 1.05mm
                      // instead of 0.75mm for the same total case_h.
fit_gap      = 0.25; // skirt-to-cavity clearance (loosen if the lid is too tight on your printer)
lid_thick    = 2.0;  // lid back-plate thickness
tab_w        = 6;    // snap-tab width
tab_reach    = 0.8;  // how far the snap tab bump projects past the skirt face
batt_z_slack = 0.2;   // battery-bay depth tolerance — deliberately a separate variable from
                      // `clearance` above: they used to be the same value reused for two
                      // unrelated things, so tightening the PCB's XY fit would have silently
                      // made the case_h computation change too (via z_batt_back). Trimmed from
                      // 0.35 to 0.2 as part of the depth-reduction rebalance (see skirt_h).
batt_l_extra = 3.0;   // extra cavity length (X axis) beyond what the PCB itself needs, purely
                      // to give the battery bay room — at batt_l=94 vs the PCB-driven in_l
                      // (~94.4 with just `clearance`), there was only ~0.4mm to spare, not
                      // enough to actually place the battery + its connector/wire.
                      // NOT added to in_w: batt_w=50 already clears in_w comfortably.
                      // Note this widens the case uniformly front-to-back (same cross-section
                      // the whole depth), so the front bezel ends up with ~1.5mm more margin
                      // on each end too — cosmetic only, doesn't touch the screen window math
                      // below (that's still centered on pcb_l, not in_l).
$fn = 64;

// ---- derived (don't hand-edit these — they follow from the above) ----
in_l  = pcb_l + 2*clearance + batt_l_extra;
in_w  = pcb_w + 2*clearance;
in_r  = pcb_corner_r + clearance;

out_l = in_l + 2*wall;
out_w = in_w + 2*wall;
out_r = in_r + wall;

lcd_module_gap = stack_d - pcb_thick;          // LCD module footprint in front of the PCB (RTP+glass+tape)
z_pcb_front  = front_wall + lcd_module_gap;    // PCB front (copper/RTP side) surface — flush LCD glass sits right at the front wall
z_pcb_back   = z_pcb_front + pcb_thick;        // PCB back (component) surface
z_comp_tip   = z_pcb_back + back_comp_h;       // tallest back-side component tip
z_batt_front = z_comp_tip + 0.3;               // clearance gap before battery bay (trimmed from
                                                // 0.6 as part of the depth-reduction rebalance)
z_batt_back  = z_batt_front + batt_t + batt_z_slack;

// The lid, once seated, has its back plate (lid_thick) flush with the case's
// back rim, and the skirt extends FORWARD from there — so the skirt's tip
// (its shallowest reach into the cavity) lands at case_h - lid_thick - skirt_h,
// not case_h - skirt_h. A previous version of this formula omitted lid_thick
// entirely, undersizing case_h by exactly 2mm (lid_thick's value) — verified
// by actually nesting the lid into the case and intersecting it against a
// modeled battery volume (not just re-checking the formula against itself,
// which is how the bug went unnoticed for several edits): that test produced
// real overlap geometry, confirming an ~1.85mm physical collision that the
// old formula's own "0.15mm clearance" claim had completely missed.
case_h       = z_batt_back + lid_thick + skirt_h + 0.15; // total case-body height (excludes lid)
cavity_depth = case_h - front_wall;            // front-wall inner face -> open back rim

// batt_l/batt_w otherwise have no effect on the geometry at all (the cavity's
// XY footprint is sized purely from the PCB) — nothing previously caught a
// battery that's too big for it, so a future edit to clearance/pcb_l/pcb_w
// could silently make the battery not fit. This just makes that loud instead.
assert(batt_l <= in_l - 0.2,
    str("battery (batt_l=", batt_l, ") doesn't fit the cavity length (in_l=", in_l, ")"));
assert(batt_w <= in_w - 0.2,
    str("battery (batt_w=", batt_w, ") doesn't fit the cavity width (in_w=", in_w, ")"));

echo(str("Case body exterior: ", out_l, " x ", out_w, " x ", case_h, " mm"));
echo(str("Cavity depth (behind front wall): ", cavity_depth, " mm"));
echo(str("Lid overall thickness (plate+skirt beyond case rim): ", lid_thick, " mm"));

module rrect(l, w, r) {
    hull() {
        for (x = [-1, 1], y = [-1, 1])
            translate([x*(l/2 - r), y*(w/2 - r)]) circle(r = r);
    }
}

module screen_window() {
    win_w = screen_w + 2*screen_reveal_extra;
    win_h = screen_h + 2*screen_reveal_extra;
    // active area sits screen_r_margin from the PCB's right edge (the FPC/
    // pull-tape edge, opposite the USB-C wall at x=-out_l/2); x_off is how
    // far the window center sits right of the PCB/case center, toward that
    // near edge.
    x_off = (pcb_l - screen_w)/2 - screen_r_margin;
    translate([x_off, 0, -1])
        linear_extrude(height = front_wall + 2)
            square([win_w, win_h], center = true);
}

module usbc_window() {
    z0 = z_pcb_back + usbc_z0;
    translate([-out_l/2 - 1, -usbc_w/2, z0])
        cube([wall + 2, usbc_w, usbc_h]);
}

// No corner screws/standoffs on this design — deliberately. A front-wall
// standoff at the real hole_l x hole_w positions is not possible without
// poking into the screen window (see the note above pcb_l/pcb_w). On the
// real board this corner is covered by the LCD module's own opaque frame
// (the "LCD BL" backlight outline is exactly pcb_w wide, i.e. it already
// spans corner-to-corner), not by anything the case contributes — so the
// case has nothing to attach a standoff to there without visibly
// overlapping the cutout.
//
// Retention instead relies on a snug friction-fit stack: front wall <-
// LCD module <- PCB <- back-side components <- battery <- lid, each
// dimensioned with only small (0.2-0.6mm) clearances (see the z_* derived
// values below), so the closed lid holds the whole assembly firmly against
// the front wall. If it ever feels loose, a small dot of foam tape behind
// the battery is the simple fix — don't add screws back in at hole_l/hole_w
// without re-deriving the math above; it doesn't fit.

module snap_slots() {
    // rectangular slots cut into the inside of the long sidewalls near the back rim,
    // matched by protruding tabs on the lid skirt (see lid()).
    //
    // This MUST be a fully enclosed pocket, not a cut that reaches the case's own
    // back edge — a previous version sized this as case_h-skirt_h/2 +/- (slot_h+0.4)/2,
    // which put the slot's outer edge at 20.29 while case_h was only 20.04: the slot
    // was open straight through to the case's own rim, with solid material in front of
    // the tab's resting position (stopping over-insertion) but NONE behind it. Nothing
    // then stopped the lid from just being lifted straight back off — a snap-fit needs
    // material on BOTH sides of the tab once seated, not just one.
    //
    // slot_roof reserves real case material above the pocket for the tab to hook
    // behind; slot_h only needs to comfortably contain the tab's own thickness
    // (skirt_h*0.5, must match lid()'s tab) plus a little insertion slack — it does
    // NOT need to reach anywhere near case_h.
    slot_roof   = 0.4;
    tab_h       = skirt_h * 0.5;             // must match the tab's thickness in lid()
    tab_center  = case_h - skirt_h/2;        // must match the tab's global z position in lid()
    slot_h      = tab_h + 1.1;               // tab thickness + insertion slack; bumped from +0.6
                                              // to +1.1 (extra 0.5mm) for easier engagement —
                                              // tab and slot were both landing around ~1mm thick,
                                              // too snug to reliably slide together
    slot_d      = wall + 1;
    slot_top    = min(tab_center + slot_h/2, case_h - slot_roof);
    z0          = slot_top - slot_h/2;
    assert(z0 - slot_h/2 <= tab_center - tab_h/2,
        "snap_slots: pocket doesn't fully contain the tab's insertion range");
    for (y = [-1, 1])
        translate([0, y*(in_w/2 + wall/2), z0])
            cube([tab_w + 0.4, slot_d, slot_h], center = true);
}

module case_body() {
    difference() {
        linear_extrude(height = case_h) rrect(out_l, out_w, out_r);
        translate([0, 0, front_wall])
            linear_extrude(height = cavity_depth + 1) rrect(in_l, in_w, in_r);
        screen_window();
        usbc_window();
        snap_slots();
    }
}

// Cantilever relief: a plain cube tab fused directly to the skirt's rigid
// wall has nothing to flex — the whole skirt would have to deform, which a
// ~2mm FDM wall won't do reliably. These cuts free a short flexible finger
// on either side of each tab so the tab itself can deflect on insertion.
tab_finger_w  = tab_w + 4;   // finger is wider than the tab so the tab sits on solid material
tab_relief_w  = 1.2;         // width of the flex-relief slot cut beside each finger
tab_relief_h  = skirt_h + 1; // spans slightly more than the full skirt height to cut cleanly through
tab_relief_overlap = 0.1;    // the finger's edge and the relief cut's inner edge would otherwise
                              // land at the exact same X coordinate (an exact tangency, not a
                              // guaranteed overlap) — this nudges the cut slightly into the
                              // finger so the boundary isn't a degenerate zero-thickness seam

module lid() {
    skirt_od_l  = in_l - 2*fit_gap;
    skirt_od_w  = in_w - 2*fit_gap;
    skirt_r     = max(in_r - fit_gap, 0.5);
    skirt_wall  = wall; // match the case's own wall thickness

    difference() {
        union() {
            // back plate, sized to the case's outer footprint so it caps the open back flush
            linear_extrude(height = lid_thick) rrect(out_l, out_w, out_r);

            // thin-walled skirt shell that plugs into the case cavity — NOT solid: a solid
            // plug this deep (skirt_h) would occupy the same space as the battery bay
            translate([0, 0, lid_thick])
                linear_extrude(height = skirt_h)
                    difference() {
                        rrect(skirt_od_l, skirt_od_w, skirt_r);
                        rrect(skirt_od_l - 2*skirt_wall, skirt_od_w - 2*skirt_wall,
                              max(skirt_r - skirt_wall, 0.5));
                    }

            // snap tabs on the two long sides of the skirt
            for (y = [-1, 1])
                translate([0, y*(skirt_od_w/2), lid_thick + skirt_h/2])
                    cube([tab_w, tab_reach*2, skirt_h*0.5], center = true);
        }

        // free-flex the tabs from the rest of the skirt wall
        for (y = [-1, 1], xs = [-1, 1])
            translate([xs*(tab_finger_w/2 + tab_relief_w/2 - tab_relief_overlap), y*(skirt_od_w/2), lid_thick + tab_relief_h/2])
                cube([tab_relief_w, wall*3, tab_relief_h], center = true);
    }
}

// ============================================================
// Select what to render/export. Change to "lid" and re-render to
// get the second STL.
// ============================================================
// Places the lid in its true seated position: back plate flush with the
// case's back rim (global z=case_h), skirt/tabs projecting forward into the
// cavity. Useful for actually verifying (or just seeing) how the snap tabs
// engage the case's slots — the two live on separate STL files, so neither
// one alone ever shows the complete retention mechanism.
module lid_seated() {
    translate([0, 0, case_h])
        mirror([0, 0, 1])
            lid();
}

PART = "body"; // "body" | "lid" | "preview" (side by side) | "assembled" (nested, as installed)
                // | "none" (render nothing — lets another file `include` this one, e.g. for a
                // cutaway/collision check, without this file's own tail adding stray geometry)

if (PART == "body") case_body();
else if (PART == "lid") lid();
else if (PART == "assembled") {
    case_body();
    color("orange") lid_seated();
}
else if (PART == "none") { }
else {
    case_body();
    translate([0, out_w + 10, 0]) lid();
}
