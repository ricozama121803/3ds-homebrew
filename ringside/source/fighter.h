// A fighter: game state, procedural pose (IK-driven skeleton) and drawing.
#pragma once
#include "vec.h"
#include "gfx.h"

// ---- body dimensions (metres) shared by the rig and the meshes
#define UPPER_ARM 0.335f
#define FORE_ARM  0.295f
#define GLOVE_OFF 0.115f         // wrist -> glove centre
#define THIGH     0.46f
#define SHIN      0.45f
#define ANKLE_H   0.085f
#define WAIST_UP  0.06f          // pelvis centre -> waist pivot
#define NECK_UP   0.50f          // waist pivot -> base of the neck
#define SHOULDER_UP 0.43f
#define SHOULDER_W  0.235f
#define HIP_W       0.100f

// ---- controls ---------------------------------------------------------------------------
enum {
	BTN_LEAD_PUNCH = 1 << 0,   // Y
	BTN_REAR_PUNCH = 1 << 1,   // X
	BTN_LEAD_KICK  = 1 << 2,   // B
	BTN_REAR_KICK  = 1 << 3,   // A
	BTN_MOD        = 1 << 4,   // L: hook modifier (with R = body block)
	BTN_GUARD      = 1 << 5,   // R: block head
	BTN_DP_LEFT    = 1 << 6,   // head movement
	BTN_DP_RIGHT   = 1 << 7,
	BTN_DP_UP      = 1 << 8,
	BTN_DP_DOWN    = 1 << 9,
};

// One frame of input for one fighter (human or AI).
typedef struct {
	float mx;                  // desired movement in *screen* directions: +1 = right (+X world)
	float mz;                  // +1 = towards the camera (+Z world), -1 = into the screen
	unsigned held;             // BTN_* currently held
	unsigned pressed;          // BTN_* pressed this frame
} FighterInput;

// ---- attacks ----------------------------------------------------------------------------
typedef enum {
	ATK_NONE = -1,
	ATK_LEAD_PUNCH, ATK_REAR_PUNCH, ATK_LEAD_HOOK, ATK_REAR_HOOK, ATK_LEAD_KICK, ATK_REAR_KICK,
	ATK_COUNT
} AttackId;

typedef struct {
	const char *name;
	int startup, active, recovery;   // frames at 60 Hz
	float reach;                     // furthest the striking limb tip can get from its root joint (m)
	float damage, stamina;
	int hitstun, blockstun;
	float push;                      // knock-back on hit (m)
	float step;                      // lunge forward while winding up (m)
	int limb;                        // 0 lead hand, 1 rear hand, 2 lead foot, 3 rear foot
	int aim_head;                    // 1 = aims at the head (body if the attacker is ducking), 0 = body
	float tip_r;                     // hit radius of the glove / foot
} AttackDef;

extern const AttackDef ATTACKS[ATK_COUNT];

// ---- pose / rig -------------------------------------------------------------------------
typedef struct {
	Vec3  pelvis;                    // pelvis centre, fighter-local (origin on the floor between the feet, +X forward)
	float pelvis_yaw, torso_yaw, torso_pitch, torso_roll;
	float head_yaw, head_pitch, head_roll;
	Vec3  hand[2];                   // glove centres (0 lead, 1 rear), fighter-local
	Vec3  foot[2];                   // ankle centres (0 lead, 1 rear), fighter-local
	float foot_yaw[2];
	float elbow_flare[2];            // 0 = elbows down, 1+ = elbows out
	float lean_x;                    // whole-body shove for hit reactions
} Pose;

typedef struct {
	Mat4 pelvis, torso, head;
	Mat4 upper_arm[2], fore_arm[2], glove[2];
	Mat4 thigh[2], shin[2], foot[2];
	Vec3 head_c, chest_c, pelvis_c;  // world-space hit-volume anchors
	Vec3 tip[4];                     // world-space glove/foot centres: lead hand, rear hand, lead foot, rear foot
	Vec3 shoulder[2];
} Rig;

typedef struct {
	Material skin, cloth, glove, hair, trim, dark;   // lighting materials; painted vertex tints do the colouring
	int body;                                        // which body/costume set to draw (0 = player, 1 = rival)
} Look;

typedef enum { ST_IDLE, ST_ATTACK, ST_HITSTUN, ST_BLOCKSTUN, ST_GUARDBREAK, ST_KNOCKDOWN, ST_KO } FState;

typedef struct Fighter {
	// placement
	float x, z;                      // world position (metres)
	float yaw;                       // facing angle about Y: 0 = facing +X
	float vx, vz;
	int   lead;                      // +1: right side leads (this fighter's right side is towards +Z when facing +X), -1: left

	// combat state
	FState st;
	int    st_t;                     // frames spent in this state
	int    st_len;                   // frames the state lasts (hit/block stun, knockdown)
	int    atk;                      // AttackId while ST_ATTACK, else ATK_NONE
	int    atk_t;
	int    atk_hit;                  // this attack already connected / was blocked
	Vec3   strike;                   // world point the striking limb is aimed at (locked early in the wind-up)
	float  lunge;                    // metres per frame moved during the wind-up (negative = bounce back), set when the attack starts
	float  hp, stamina;              // 0..100
	int    guard;                    // 0 none, 1 head, 2 body (held)
	float  guard_amt[2];             // smoothed 0..1 for head / body guard
	float  lean, duck, slip;         // smoothed head movement: back(-)/forward(+), duck 0..1, slip -1..1
	float  react;                    // 0..1 hit reaction envelope
	Vec3   react_dir;                // world direction the hit pushed us
	int    react_zone;               // 0 body, 1 head
	float  walk_phase, walk_amt;
	float  fall;                     // 0..1 how far down a KO/knockdown is
	float  bob_t;
	float  kx, kz;                   // knock-back velocity (decays)
	int    down_t;                   // frames spent on the canvas
	int    hit_taken;                // count of hits landed on us (HUD/AI use)

	Mat4  trail[4];                  // recent world matrices of the striking limb, newest first (afterimage)
	int   trail_n;
	Look  look;
	Pose  pose;
	Rig   rig;                       // world-space, refreshed by fighter_pose()
} Fighter;

void fighter_init(Fighter *f, float x, float z, float yaw, int lead, const Look *look);
Look look_player(void);
Look look_opponent(void);

// Rebuild pose + rig from the fighter's current state. `t` is game time in seconds, `foe` is who we face.
void fighter_pose(Fighter *f, float t);
void fighter_record_trail(Fighter *f);            // call once per game tick after fighter_pose

void fighter_meshes_build(void);
void fighter_set_detail(int high);                  // 0 = skip outlines and small details (Old 3DS)
void fighter_draw(const Fighter *f);
void fighter_draw_shadow(const Fighter *f);
void fighter_draw_trail(const Fighter *f);         // motion afterimage of a fast strike (blended, draw last)
Mat4 fighter_matrix(const Fighter *f);             // fighter-local -> world
Vec3 fighter_to_local(const Fighter *f, Vec3 w);
