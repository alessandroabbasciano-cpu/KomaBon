// =============================================================================
// PROJECT: KomaBon Enclosure (TRMNL 7.5" + Seeed Studio XIAO ESP32-S3)
// ARCHITECTURE: Horizontal flange split-shell sandwich (M2 fasteners)
// COORDINATE SYSTEM: Origin (0,0,0) at the geometric center of the active screen window
// =============================================================================

$fn = 50; // Mesh polygon resolution for holes, spheres, and cylinders

// =============================================================================
// SECTION 1: GLOBAL DISPLAY PANEL PARAMETERS AND MARGINS (LOCKED)
// =============================================================================
screen_window_L = 165.2; // Active TRMNL viewport length (X axis)
screen_window_W = 100.1; // Active TRMNL viewport width (Y axis)

margin_top = 2.5;    // Top border (Y+)
margin_bottom = 9.5; // Bottom border (Y-) -> display flex cable exit side
margin_left = 2.5;   // Left border (X-)
margin_right = 2.5;  // Right border (X+)

overhang_top = 10.0;   // Top perimeter extension
overhang_bottom = 3.0; // Bottom perimeter extension (optimized for flex cable mating)
overhang_left = 6.0;   // Left perimeter extension
overhang_right = 6.0;  // Right perimeter extension

outer_wall_t = 1.6; // Nominal structural wall thickness
boss_D = 7.0;       // Outer diameter and safety margin for M2 corner mounting bosses

// =============================================================================
// SECTION 2: DERIVED GEOMETRIC VECTORS AND ASYMMETRY COMPENSATION
// =============================================================================
panel_L = screen_window_L + margin_left + margin_right; // 170.2 mm
panel_W = screen_window_W + margin_top + margin_bottom; // 112.1 mm

panel_offset_X = (margin_right - margin_left) / 2;
panel_offset_Y = (margin_top - margin_bottom) / 2;

case_L = panel_L + overhang_left + overhang_right;
case_W = panel_W + overhang_top + overhang_bottom;

case_offset_X = panel_offset_X + (overhang_right - overhang_left) / 2;
case_offset_Y = panel_offset_Y + (overhang_top - overhang_bottom) / 2;

// =============================================================================
// SECTION 3: FRONT FRAME PARAMETERS (TOP FRAME) AND PERIMETER INTERLOCK
// =============================================================================
front_floor_t = 1.8;     // Z shoulder height: 2.0 mm deep screen pocket
front_H = 3.8;           // Total front bezel thickness
frame_bevel_width = 7.0; // Picture-frame bevel width around the display
bevel_lip_h = 0.6;       // Vertical 90° edge height (fixed Y, Z transition)
front_corner_r = 4.0;    // Corner radius for the 4 vertical perimeter edges
front_edge_r = 3.0;      // Continuous smooth fillet radius for top outer lip

cable_gap_width = 25.0;                                    // Width of display flex cable passthrough slot
cable_pocket_depth = overhang_bottom - outer_wall_t + 0.6; // Flex cable pocket depth

insert_d = 3.2;         // Blind pilot hole diameter for M2 brass threaded heat-set inserts
hole_depth = 3.2;       // Blind hole depth for brass insert
insert_chamfer_d = 4.3; // Outer diameter for melt-flash relief countersink
insert_chamfer_h = 1.5; // 45° lead-in countersink depth

// Perimeter Tongue & Groove parameters
joint_lip_h = 1.2;       // Male tongue height
joint_lip_w = 1.2;       // Nominal male tongue width
joint_tol = 0.25;        // Radial FDM clearance per side (1.7 mm wide groove)
joint_inset = 2.0;       // Tongue centerline inset from outer perimeter
joint_side_y_len = 96.0; // Tongue length on short sides (centered in Y)
joint_top_x_len = 140.0; // Tongue length on top long side (centered in X)

// =============================================================================
// SECTION 4: REAR SHELL PARAMETERS (BOTTOM TUB) AND INTERNAL ELECTRONICS
// =============================================================================
back_H = 10;              // Total rear shell depth
back_floor_t = 1.6;       // Floor thickness supporting internal components
back_fillet_radius = 4.0; // Outer corner fillet radius for mating flange
flange_t = 3.0;           // Horizontal flat closing flange thickness (screw shoulder)
flange_edge_r = 2.0;      // Continuous outer chamfer/fillet radius for flange

// M2 socket head cap screw counterbore (DIN 912: head h=2.0, d=3.82)
screw_head_d = 4.3; // Counterbore diameter (3.82 mm + FDM tolerance)
screw_head_h = 2.0; // Counterbore depth (leaves 1.0 mm clamping shoulder)

// Mirrored flex cable pocket on rear shell
cable_pocket_back_h = 1.5; // Z depth of flex cable relief on rear shell

flange_brim = 4.0; // Perimeter brim width before bottom slope
chamfer_x = 30.0;  // Horizontal slope travel along X axis
chamfer_y = 6.0;   // Horizontal slope travel along Y axis

tub_corner_r_top = 8.0;  // Tub corner radius at flange junction
tub_corner_r_bot = 25.0; // Tub corner radius at flat bottom transition

tub_inner_flat_x = (case_L - chamfer_x * 2 - outer_wall_t * 2) / 2;
tub_slope_len_x = chamfer_x - flange_brim;
tub_inner_H = (back_H - flange_t) - back_floor_t;

// --- A. Motherboard (Seeed Studio XIAO ESP32-S3 carrier board) ---
mb_L = 80.1;
mb_W = 41.25;
mb_hole_X = 75.22;
mb_hole_Y = 36.30;
mb_standoff_H = 7.0;
pos_mb_X = 6.0;
pos_mb_Y = -35.5;

// Parameters for motherboard recess pocket and locating alignment pins
mb_pin_target_d = 2.2; // Target diameter at resting shoulder (2.5 mm height)
mb_pocket_depth = 0.8; // Floor recess pocket depth
mb_pocket_tol = 0.4;   // XY tolerance for board pocket

// Motherboard I/O cutout parameters (calibrated dimensional chain)
mb_usb_on_right = false; // false: Switch at +X and USB at -X. true: inverted.

ui_z_center = back_floor_t + 4.5; // Global Z center for ports and button cutouts

usb_cut_w = 12.0;      // USB-C connector cutout width
usb_cut_h = 5.5;       // USB-C connector cutout height
usb_corner_r = 2.0;    // USB-C cutout corner radius
switch_cut_w = 9.5;    // Power switch travel slot length
switch_cut_h = 5.0;    // Power switch slot height
switch_corner_r = 1.5; // Switch slot corner radius
btn_hole_d = 4.0;      // Access hole diameter for buttons

offset_sw_center = 8.0; // Calibrated switch center offset
offset_btn1 = 18.0;
offset_btn2 = 27.5;
offset_btn3 = 37.0;
offset_btn4 = 46.5;
offset_usb = 63.0;

// --- B. LiPo Battery Compartment ---
batt_L = 60.5;
batt_W = 46.0;
batt_wall_H = 7.0;
batt_side_cut = 10.0;
pos_batt_X = 5.0;
pos_batt_Y = 20.5;

// --- C. MicroSD Module (Tilted Tunnel Slot with Roof) ---
sd_L = 10.0;
sd_W = 18.0;
sd_wall_H = 3.5;
sd_roof_t = 0.8;
sd_cutout_L = 15.0;
sd_tilt_angle = 8.0; // Tilt angle (raises rear by ~1.5 mm for easy card extraction)
pos_sd_X = -(case_L / 2) + outer_wall_t + (sd_L / 2) + chamfer_x * 1.0;
pos_sd_Y = pos_mb_Y - 2.0;

// --- D. 5-Way Caddx Joystick ---
js_L = 10.5;
js_W = 10.5;
js_wall_H = 6.0;       // Vertical 90° sidewall height from floor
joystick_hole_D = 8.0; // Shaft clearance hole diameter
js_collar_H = 3.0;     // Adjustable central support collar height
js_collar_t = 1.2;     // Collar wall thickness
js_bevel_depth = 1.0;  // Internal chamfer depth (0.5 - 1.0 mm)
js_bevel_w = 20.0;     // Outer square-to-round bezel width
pos_js_X = (case_L / 2) - outer_wall_t - (js_L / 2) - (chamfer_x * 1.1);
pos_js_Y = 0;

// --- E. Screen Support Ribs (Anti-Fulcrum Clearance) ---
screen_support_corner_x = 16.0;  // Compact X extension for corner support
screen_support_corner_y = 14.0;  // Compact Y extension for corner support
screen_support_corner_r = 6.0;   // Internal corner blend radius
screen_support_H = back_H - 0.3; // 9.7 mm: clearance relief to eliminate display fulcrum stress

// --- F. Power Switch Lever Clip with Rear Retaining Tongue ---
clip_total_w = 5.5;    // Slider width (leaves 4.0 mm usable travel in 9.5 mm slot)
clip_total_h = 4.2;    // Slider height (clears freely in 5.0 mm slot)
clip_cap_t = 2.4;      // Outer button cap thickness
clip_stem_depth = 2.4; // Stem engagement depth over switch lever
sw_lever_w = 1.55;     // Slot width for SMD switch lever (friction fit)
clip_tongue_len = 7.0; // Side retention tongue extension length
clip_tongue_t = 0.6;   // Retention tongue thickness
clip_tongue_dir = 1;   // Direction: 1 = toward free corner (+X), -1 = toward buttons (-X)

// =============================================================================
// SECTION 5: PROCEDURAL GEOMETRIC MODULES AND ANALYTICAL FUNCTIONS
// =============================================================================

function get_inner_floor_z(x_pos) = (abs(x_pos) <= tub_inner_flat_x) ? back_floor_t
                                    : (abs(x_pos) >= (tub_inner_flat_x + tub_slope_len_x))
                                        ? (back_H - flange_t)
                                        : back_floor_t
                                              + tub_inner_H *
                                                    pow((abs(x_pos) - tub_inner_flat_x) / tub_slope_len_x, 1.8);

module rounded_rect_slice(L, W, r, z, h=0.01) {
    translate([0, 0, z]) hull() {
        translate([ L/2 - r,  W/2 - r, 0]) cylinder(r=r, h=h);
translate([ -L / 2 + r, W / 2 - r, 0 ])
cylinder(r = r, h = h);
translate([ L / 2 - r, -W / 2 + r, 0 ])
cylinder(r = r, h = h);
translate([ -L / 2 + r, -W / 2 + r, 0 ])
cylinder(r = r, h = h);
}
}

// Generates rounded-corner slot cutouts on vertical Y wall
module rounded_cutout_y(w, h, thick, r) {
    rotate([90, 0, 0])
    hull() {
        translate([ w/2 - r,  h/2 - r, 0]) cylinder(r=r, h=thick, center=true);
translate([ -w / 2 + r, h / 2 - r, 0 ])
cylinder(r = r, h = thick, center = true);
translate([ w / 2 - r, -h / 2 + r, 0 ])
cylinder(r = r, h = thick, center = true);
translate([ -w / 2 + r, -h / 2 + r, 0 ])
cylinder(r = r, h = thick, center = true);
}
}

// Generates volumetric perimeter ribs for Tongue & Groove joint
module joint_ribs(tol = 0, h = joint_lip_h, anchor = 0, taper = 0){w = joint_lip_w + tol * 2;
r1 = max(0.2, w / 2);
r2 = max(0.2, (w - taper * 2) / 2);
total_h = h + anchor;

// Short sides (X- and X+)
for (sx = [ -1, 1 ])
{
	x_pos = case_offset_X + sx * (case_L / 2 - joint_inset);
	translate([ x_pos, case_offset_Y, -anchor / 2 ])
	hull()
	{
		translate([ 0, joint_side_y_len / 2 - r1, 0 ])
		cylinder(r1 = r1, r2 = r2, h = total_h, center = true);
		translate([ 0, -joint_side_y_len / 2 + r1, 0 ])
		cylinder(r1 = r1, r2 = r2, h = total_h, center = true);
	}
}

// Top long side (Y+)
y_pos = case_offset_Y + case_W / 2 - joint_inset;
translate([ case_offset_X, y_pos, -anchor / 2 ])
hull()
{
	translate([ joint_top_x_len / 2 - r1, 0, 0 ])
	cylinder(r1 = r1, r2 = r2, h = total_h, center = true);
	translate([ -joint_top_x_len / 2 + r1, 0, 0 ])
	cylinder(r1 = r1, r2 = r2, h = total_h, center = true);
}
}

module front_smooth_frame(L, W, H, r_corner, r_edge, steps = 16){straight_H = H - r_edge;
if (straight_H > 0)
{
	rounded_rect_slice(L, W, r_corner, r_edge, straight_H);
}
for (i = [0:steps - 1])
{
	a1 = (i / steps) * 90;
	a2 = ((i + 1) / steps) * 90;
	z1 = r_edge * (1 - cos(a1));
	z2 = r_edge * (1 - cos(a2));
	inset1 = r_edge * (1 - sin(a1));
	inset2 = r_edge * (1 - sin(a2));
	hull()
	{
		rounded_rect_slice(L - inset1 * 2, W - inset1 * 2, max(0.5, r_corner - inset1), z1, 0.01);
		rounded_rect_slice(L - inset2 * 2, W - inset2 * 2, max(0.5, r_corner - inset2), z2, 0.01);
	}
}
}

module hybrid_tub_outer(L_top, W_top, L_bot, W_bot, r_top, r_bot, H, steps = 24){for (i = [0 :steps - 1]){s1 = i / steps;
s2 = (i + 1) / steps;
z1 = H * pow(s1, 1.8);
z2 = H * pow(s2, 1.8);
L1 = L_bot + (L_top - L_bot) * s1;
W1 = W_bot + (W_top - W_bot) * s1;
r1 = r_bot + (r_top - r_bot) * s1;
L2 = L_bot + (L_top - L_bot) * s2;
W2 = W_bot + (W_top - W_bot) * s2;
r2 = r_bot + (r_top - r_bot) * s2;
hull()
{
	rounded_rect_slice(L1, W1, r1, z1, 0.01);
	rounded_rect_slice(L2, W2, r2, z2, 0.01);
}
}
}

module hybrid_tub_inner(L_top, W_top, L_bot, W_bot, r_top, r_bot, H, floor_t, wall_t, steps = 24){inner_H = H - floor_t;
for (i = [0:steps - 1])
{
	s1 = i / steps;
	z1 = floor_t + inner_H * pow(s1, 1.8);
	s2 = (i + 1) / steps;
	z2 = floor_t + inner_H * pow(s2, 1.8);
	L1 = (L_bot - wall_t * 2) + ((L_top - wall_t * 2) - (L_bot - wall_t * 2)) * s1;
	W1 = (W_bot - wall_t * 2) + ((W_top - wall_t * 2) - (W_bot - wall_t * 2)) * s1;
	r1 = max(0.5, (r_bot - wall_t) + ((r_top - wall_t) - (r_bot - wall_t)) * s1);
	L2 = (L_bot - wall_t * 2) + ((L_top - wall_t * 2) - (L_bot - wall_t * 2)) * s2;
	W2 = (W_bot - wall_t * 2) + ((W_top - wall_t * 2) - (W_bot - wall_t * 2)) * s2;
	r2 = max(0.5, (r_bot - wall_t) + ((r_top - wall_t) - (r_bot - wall_t)) * s2);
	hull()
	{
		rounded_rect_slice(L1, W1, r1, z1, 0.01);
		rounded_rect_slice(L2, W2, r2, z2, 0.01);
	}
}
rounded_rect_slice(L_top - wall_t * 2, W_top - wall_t * 2, max(0.5, r_top - wall_t), H - 0.01, flange_t + 0.2);
}

// Power switch slider clip with side retaining tongue
// Tongue is aligned to +Y (inner/rear face of the slider body)
module switch_lever_clip(){total_len = clip_cap_t + clip_stem_depth;
overlap = 0.2; // Overlap to prevent non-manifold artifacts

translate([ 0, 0, clip_total_h / 2 ])
{
	difference()
	{
		union()
		{
			// 1. Shaped main body
			hull()
			{
				r = 0.8;
				translate([ clip_total_w / 2 - r, total_len / 2 - r, 0 ])
				cylinder(r = r, h = clip_total_h, center = true);
				translate([ -clip_total_w / 2 + r, total_len / 2 - r, 0 ])
				cylinder(r = r, h = clip_total_h, center = true);
				translate([ clip_total_w / 2 - r, -total_len / 2 + r, 0 ])
				cylinder(r = r, h = clip_total_h, center = true);
				translate([ -clip_total_w / 2 + r, -total_len / 2 + r, 0 ])
				cylinder(r = r, h = clip_total_h, center = true);
			}

			// 2. Side retaining tongue (coplanar with rear +Y face)
			tongue_x_mid = clip_tongue_dir * (clip_total_w / 2 + clip_tongue_len / 2 - overlap / 2);
			tongue_y_mid = total_len / 2 - clip_tongue_t / 2;

			translate([ tongue_x_mid, tongue_y_mid, 0 ])
			cube([ clip_tongue_len + overlap, clip_tongue_t, clip_total_h ], center = true);
		}

		// Vertical pass-through C-slot (relieves Z axis, locks X axis)
		translate([ 0, total_len / 2 - clip_stem_depth / 2 + 0.05, 0 ])
		cube([ sw_lever_w, clip_stem_depth + 0.1, clip_total_h + 0.2 ], center = true);

		// Vertical serrations for thumb grip on front face (-Y)
		for (gx = [ -1.5, 0, 1.5 ])
		{
			translate([ gx, -total_len / 2, 0 ])
			rotate([ 0, 0, 45 ])
			cube([ 0.5, 0.5, clip_total_h + 0.2 ], center = true);
		}
	}
}
}

// =============================================================================
// SECTION 6: MODULE 1 - FRONT PANEL (TOP FRAME)
// =============================================================================
module front_panel() {
    union() {
        difference() {
            translate([case_offset_X, case_offset_Y, 0])
                front_smooth_frame(case_L, case_W, front_H, front_corner_r, front_edge_r);

// Display pocket enlarged to 2.0 mm depth (Z from 1.8 to 3.8 mm)
translate([ panel_offset_X, panel_offset_Y, front_floor_t + front_H / 2 ])
cube([ panel_L, panel_W, front_H + 0.1 ], center = true);

hull()
{
	translate([ 0, 0, -0.1 ])
	cube([ screen_window_L + frame_bevel_width * 2, screen_window_W + frame_bevel_width * 2, 0.1 ], center = true);
	translate([ 0, 0, front_floor_t - bevel_lip_h ])
	cube([ screen_window_L, screen_window_W, 0.02 ], center = true);
}
translate([ 0, 0, (front_floor_t - bevel_lip_h + front_floor_t) / 2 ])
cube([ screen_window_L, screen_window_W, bevel_lip_h + 0.02 ], center = true);

if (cable_pocket_depth > 0)
{
	translate(
	    [ panel_offset_X, panel_offset_Y - panel_W / 2 - cable_pocket_depth / 2 + 0.01, front_floor_t + front_H / 2 ])
	cube([ cable_gap_width, cable_pocket_depth + 0.02, front_H ], center = true);
}

for (x = [ -1, 1 ])
{
	for (y = [ -1, 1 ])
	{
		translate([ case_offset_X + x * (case_L / 2 - boss_D / 2), case_offset_Y + y * (case_W / 2 - boss_D / 2), 0 ])
		{
			translate([ 0, 0, front_H - hole_depth ])
			cylinder(h = hole_depth + 0.1, d = insert_d);
			translate([ 0, 0, front_H - insert_chamfer_h ])
			cylinder(h = insert_chamfer_h + 0.1, d1 = insert_d, d2 = insert_chamfer_d);
		}
	}
}
}

// Perimeter male tongue (Tongue & Groove, anchored inside bezel body)
translate([ 0, 0, front_H + joint_lip_h / 2 ])
joint_ribs(tol = 0, h = joint_lip_h, anchor = 0.8, taper = 0.25);
}
}

// =============================================================================
// SECTION 7: MODULE 2 - REAR SHELL (BOTTOM TUB)
// =============================================================================
module back_panel(){tub_H = back_H - flange_t;
L_top = case_L - flange_brim * 2;
W_top = case_W - flange_brim * 2;
L_bot = case_L - chamfer_x * 2;
W_bot = case_W - chamfer_y * 2;

mb_wall_cut_y = case_offset_Y - case_W / 2 + flange_brim / 2;
mb_cut_thickness = 15.0;

mb_sw_hole_x = case_offset_X + pos_mb_X + (mb_usb_on_right ? (-mb_hole_X / 2) : (mb_hole_X / 2));
mb_dir_x = mb_usb_on_right ? 1 : -1;

x_sw = mb_sw_hole_x + mb_dir_x * offset_sw_center;
x_btn1 = mb_sw_hole_x + mb_dir_x * offset_btn1;
x_btn2 = mb_sw_hole_x + mb_dir_x * offset_btn2;
x_btn3 = mb_sw_hole_x + mb_dir_x * offset_btn3;
x_btn4 = mb_sw_hole_x + mb_dir_x * offset_btn4;
x_usb = mb_sw_hole_x + mb_dir_x * offset_usb;

sd_total_H = sd_wall_H + sd_roof_t;
sd_front_x = case_offset_X + pos_sd_X - sd_L / 2 - 3.5;
sd_cut_x_mid = sd_front_x - sd_cutout_L / 2;
sd_cut_z_h = sd_wall_H + 2.5;
sd_cut_z_mid = back_floor_t + sd_cut_z_h / 2 - 1;

cable_cut_y_outer = panel_offset_Y - panel_W / 2 - cable_pocket_depth;
cable_cut_y_inner = case_offset_Y - (case_W / 2 - flange_brim - outer_wall_t) + 1.0;
cable_cut_y_len = abs(cable_cut_y_outer - cable_cut_y_inner);
cable_cut_y_mid = (cable_cut_y_outer + cable_cut_y_inner) / 2;
cable_cut_z_h = cable_pocket_back_h;
cable_cut_z_mid = back_H - cable_cut_z_h / 2;

anchor = 0.5; // Internal overlap depth for seamless STL boolean union

// Global union to preserve positive features from later subtractions
union()
{
	difference()
	{
		// --- 7.1 POSITIVE BASE SHELL UNION ---
		union()
		{
			difference()
			{
				union()
				{
					translate([ case_offset_X, case_offset_Y, tub_H ])
					front_smooth_frame(case_L, case_W, flange_t, back_fillet_radius, flange_edge_r);

					translate([ case_offset_X, case_offset_Y, 0 ])
					hybrid_tub_outer(L_top, W_top, L_bot, W_bot, tub_corner_r_top, tub_corner_r_bot, tub_H);
				}

				translate([ case_offset_X, case_offset_Y, 0 ])
				hybrid_tub_inner(L_top, W_top, L_bot, W_bot, tub_corner_r_top, tub_corner_r_bot, tub_H, back_floor_t,
				                 outer_wall_t);
			}

			// 1. Battery compartment rear wall (retains LiPo at standard depth)
			translate([
				case_offset_X + pos_batt_X, case_offset_Y + pos_batt_Y + batt_W / 2 + outer_wall_t / 2,
				back_floor_t - anchor + (batt_wall_H + anchor) / 2
			])
			cube([ batt_L + outer_wall_t * 2, outer_wall_t, batt_wall_H + anchor ], center = true);

			// 2. Battery compartment sidewalls with anti-fulcrum relief (Z = screen_support_H = 9.7 mm)
			intersection()
			{
				union()
				{
					for (sx = [ -1, 1 ])
					{
						x_pos = case_offset_X + pos_batt_X + sx * (batt_L + outer_wall_t) / 2;
						y_start = case_offset_Y + pos_batt_Y - batt_W / 2 + batt_side_cut;
						y_end = case_offset_Y + case_W / 2;
						y_len = y_end - y_start;
						translate([ x_pos, (y_start + y_end) / 2, (back_floor_t - anchor + screen_support_H) / 2 ])
						cube([ outer_wall_t, y_len, screen_support_H - (back_floor_t - anchor) ], center = true);
					}
				}
				translate([ case_offset_X, case_offset_Y, 0 ])
				hybrid_tub_inner(L_top, W_top, L_bot, W_bot, tub_corner_r_top, tub_corner_r_bot, tub_H, back_floor_t,
				                 outer_wall_t);
			}

			// 3. Top corner supports with anti-fulcrum relief (Z = screen_support_H = 9.7 mm)
			intersection()
			{
				union()
				{
					for (sx = [ -1, 1 ])
					{
						translate([ case_offset_X, case_offset_Y, back_floor_t - anchor ])
						{
							linear_extrude(height = screen_support_H - (back_floor_t - anchor))
							{
								hull()
								{
									translate([ sx * (case_L / 2), case_W / 2 ])
									circle(r = 1);
									translate([ sx * (case_L / 2 - screen_support_corner_x), case_W / 2 ])
									circle(r = 1);
									translate([ sx * (case_L / 2), case_W / 2 - screen_support_corner_y ])
									circle(r = 1);
									translate([
										sx * (case_L / 2 - screen_support_corner_x + screen_support_corner_r),
										case_W / 2 - screen_support_corner_y +
										screen_support_corner_r
									])
									circle(r = screen_support_corner_r);
								}
							}
						}
					}
				}
				translate([ case_offset_X, case_offset_Y, 0 ])
				hybrid_tub_inner(L_top, W_top, L_bot, W_bot, tub_corner_r_top, tub_corner_r_bot, tub_H, back_floor_t,
				                 outer_wall_t);
			}

			// 4. Joystick 90° lateral sidewall supports (anchored and sliced along the shell curve)
			intersection()
			{
				union()
				{
					for (sy = [ -1, 1 ])
					{
						translate([
							case_offset_X + pos_js_X, case_offset_Y + pos_js_Y + sy * (js_W / 2 + outer_wall_t / 2),
							back_floor_t - anchor + (js_wall_H + anchor) / 2
						])
						cube([ js_L, outer_wall_t, js_wall_H + anchor ], center = true);
					}
				}
				translate([ case_offset_X, case_offset_Y, 0 ])
				hybrid_tub_inner(L_top, W_top, L_bot, W_bot, tub_corner_r_top, tub_corner_r_bot, tub_H, back_floor_t,
				                 outer_wall_t);
			}

			// 5. Joystick central support (anchored adjustable collar)
			translate([ case_offset_X + pos_js_X, case_offset_Y + pos_js_Y, back_floor_t - anchor ])
			{
				difference()
				{
					cylinder(h = js_collar_H + anchor, d = joystick_hole_D + js_collar_t * 2);
					translate([ 0, 0, -0.1 ])
					cylinder(h = js_collar_H + anchor + 0.2, d = joystick_hole_D);
				}
			}

			// 6. MicroSD tunnel compartment with closed top roof (tilted and anchored)
			translate([ case_offset_X + pos_sd_X, case_offset_Y + pos_sd_Y, back_floor_t ])
			{
				translate([ -sd_L / 2, 0, 0 ])
				rotate([ 0, -sd_tilt_angle, 0 ])
				translate([ sd_L / 2, 0, 0 ])
				{
					difference()
					{
						translate([ 0, 0, (sd_total_H + 3.0) / 2 - 1.5 ])
						cube([ sd_L, sd_W + outer_wall_t * 2, sd_total_H + 3.0 ], center = true);

						translate([ 0, 0, sd_wall_H / 2 ])
						cube([ sd_L + 2.0, sd_W, sd_wall_H + 0.05 ], center = true);
					}
				}
			}
		}

		// --- 7.2 GLOBAL SUBTRACTIONS ---

		// Perimeter female groove for Tongue & Groove joint (depth 1.5 mm at Z = back_H)
		translate([ 0, 0, back_H - (joint_lip_h + 0.3) / 2 + 0.05 ])
		joint_ribs(tol = joint_tol, h = joint_lip_h + 0.3 + 0.1, anchor = 0, taper = 0);

		// M2 through-holes with cylindrical counterbores for screw heads
		for (x = [ -1, 1 ])
		{
			for (y = [ -1, 1 ])
			{
				translate(
				    [ case_offset_X + x * (case_L / 2 - boss_D / 2), case_offset_Y + y * (case_W / 2 - boss_D / 2), 0 ])
				{
					translate([ 0, 0, -5 ])
					cylinder(h = 20, d = 2.4);

					translate([ 0, 0, tub_H - 0.1 ])
					cylinder(h = screw_head_h + 0.1, d = screw_head_d);
				}
			}
		}

		// Joystick through-hole with internal lead-in chamfer
		translate([ case_offset_X + pos_js_X, case_offset_Y + pos_js_Y, back_floor_t ])
		{
			translate([ 0, 0, -5 ])
			cylinder(h = 35, d = joystick_hole_D);
			translate([ 0, 0, js_collar_H - js_bevel_depth ])
			cylinder(h = js_bevel_depth + 0.1, d1 = joystick_hole_D, d2 = joystick_hole_D + 2.0);
		}

		// Outer square-to-round bezel (rear face of case)
		translate([ case_offset_X + pos_js_X, case_offset_Y + pos_js_Y, 0 ])
		{
			hull()
			{
				translate([ 0, 0, -0.5 ])
				cube([ js_bevel_w, js_bevel_w, 0.2 ], center = true);
				translate([ 0, 0, js_bevel_depth ])
				cylinder(h = 0.1, d = joystick_hole_D, center = true);
			}
		}

		// MicroSD access slot with outer cylindrical blend on -X face
		translate([ sd_cut_x_mid + 2.0, case_offset_Y + pos_sd_Y, sd_cut_z_mid ])
		{
			union()
			{
				cube([ sd_cutout_L - 6.0, sd_W, sd_cut_z_h + 2 ], center = true);
				translate([ -(sd_cutout_L - 6.0) / 2, 0, 0 ])
				cylinder(d = sd_W, h = sd_cut_z_h + 2, center = true);
			}
		}

		// Mirrored display flex cable relief pocket
		translate([ panel_offset_X, cable_cut_y_mid, cable_cut_z_mid + 0.05 ])
		cube([ cable_gap_width, cable_cut_y_len + 0.02, cable_cut_z_h + 0.1 ], center = true);

		// --- MOTHERBOARD CUTOUTS & RELIEFS ---

		// A. Floor recess pocket for motherboard PCB
		translate([ case_offset_X + pos_mb_X, case_offset_Y + pos_mb_Y, back_floor_t - mb_pocket_depth / 2 + 0.05 ])
		cube([ (mb_L + 2.0) + mb_pocket_tol, mb_W + mb_pocket_tol, mb_pocket_depth + 0.1 ], center = true);

		// B. Internal clearance cutout for side ports
		translate([
			case_offset_X + pos_mb_X, case_offset_Y + pos_mb_Y - mb_W / 2 + 1.3,
			back_floor_t + (tub_H - back_floor_t) / 2
		])
		cube([ mb_L + 2.0, 3.0, tub_H - back_floor_t ], center = true);

		// C. Joystick wiring escape ramp
		hull()
		{
			translate([
				case_offset_X + pos_mb_X, case_offset_Y + pos_mb_Y + mb_W / 2, back_floor_t - mb_pocket_depth / 2 + 0.05
			])
			cube([ 14.0, 0.1, mb_pocket_depth + 0.1 ], center = true);
			translate([ case_offset_X + pos_mb_X, case_offset_Y + pos_mb_Y + mb_W / 2 + 6.0, back_floor_t + 0.05 ])
			cube([ 10.0, 0.1, 0.1 ], center = true);
		}

		// 1. Rounded USB-C connector cutout
		translate([ x_usb, mb_wall_cut_y, ui_z_center + 0.5 ])
		rounded_cutout_y(usb_cut_w, usb_cut_h, mb_cut_thickness, usb_corner_r);

		// 2. Power switch slot cutout
		translate([ x_sw, mb_wall_cut_y, ui_z_center + 0.5 ])
		rounded_cutout_y(switch_cut_w, switch_cut_h, mb_cut_thickness, switch_corner_r);

		// 3. Access holes (d=4.0 mm) for buttons
		for (bx = [ x_btn1, x_btn2, x_btn3, x_btn4 ])
		{
			translate([ bx, mb_wall_cut_y, ui_z_center ])
			rotate([ 90, 0, 0 ])
			cylinder(h = mb_cut_thickness, d = btn_hole_d, center = true);
		}
	} // End Subtractions

	// --- PROTECTED POSITIVE FEATURES ---

	// Conical alignment pins for Motherboard mounting
	for (x = [ -1, 1 ])
	{
		y = 1;
		translate([
			case_offset_X + pos_mb_X + x * (mb_hole_X / 2), case_offset_Y + pos_mb_Y + y * (mb_hole_Y / 2),
			back_floor_t - mb_pocket_depth -
			anchor
		])
		{
			cylinder(h = anchor, d = 3.5);
			translate([ 0, 0, anchor ])
			{
				cylinder(h = 2.5, d1 = 3.5, d2 = mb_pin_target_d);
				translate([ 0, 0, 2.5 ])
				cylinder(h = mb_standoff_H - 2.5, d1 = mb_pin_target_d, d2 = 1.4);
			}
		}
	}

} // End Global Union
}

// =============================================================================
// SECTION 8: ASSEMBLY LAYOUT
// =============================================================================
translate([ 0, case_W / 2 + 10, 0 ])
rotate([ 0, 0, 180 ])
front_panel();

translate([ 0, -case_W / 2 - 10, 0 ])
back_panel();

// Mini-clip with side retaining tongue for SMD power switch lever
translate([ case_L / 2 + 15, -case_W / 2 - 10, 0 ])
switch_lever_clip();
