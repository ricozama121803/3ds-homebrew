// Render-path checks: support contacts, rigid bone reach, complete meshes and stereo idempotence.
// gcc -O2 -Isource -Wno-misleading-indentation -o /tmp/animtest host/animtest.c -lm
#define HOST_PREVIEW 1
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/scene.c"
#include <stdio.h>
#include <stdlib.h>

static Game G;
static Vtx verts[CAP_DYN], copy[CAP_D_HERO];
static int failures, expected;
static float maxSlip, maxBoneError;
static void render(void)
{
	int offset = 0;
	const int cap[D_COUNT] = { CAP_D_OPAQUE, CAP_D_HERO, CAP_D_ALPHA, CAP_D_ADD };
	for (int i = 0; i < D_COUNT; i++) { dm[i] = (Mesh){0}; dm[i].v = verts + offset; dm[i].cap = cap[i]; offset += cap[i]; }
	draw_hero(&G);
	if (dm[D_HERO].n != expected) failures++;
	for (int i = 0; i < dm[D_HERO].n; i++) {
		Vtx v = dm[D_HERO].v[i];
		if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z)) failures++;
	}
	for (int s = 0; s < 2; s++) {
		float err = fmaxf(fabsf(vlen(sub(heroPose.hip[s],heroPose.knee[s]))-.50f), fabsf(vlen(sub(heroPose.knee[s],heroPose.ankle[s]))-.50f));
		err = fmaxf(err, fabsf(vlen(sub(heroPose.shoulder[s],heroPose.elbow[s]))-.30f));
		err = fmaxf(err, fabsf(vlen(sub(heroPose.elbow[s],heroPose.wrist[s]))-.29f));
		maxBoneError = fmaxf(maxBoneError, err);
		if (err > .012f) { failures++; printf("reach t%.3f speed%.2f phase%.3f hip %.2f %.2f %.2f ankle %.2f %.2f %.2f err%.3f\n",G.time,vlen(v3(G.p.v[0],0,G.p.v[2])),an.gait,heroPose.hip[s].x,heroPose.hip[s].y,heroPose.hip[s].z,heroPose.ankle[s].x,heroPose.ankle[s].y,heroPose.ankle[s].z,err); }
	}
}
static void locomotion(float speed, int fps, bool wall)
{
	memset(&G, 0, sizeof G); memset(&an, 0, sizeof an);
	G.p.state = wall ? ST_WALL : ST_GROUND; G.p.wallRun = wall;
	G.p.wn[1] = -1; G.p.face = PI_F; G.p.v[wall ? 0 : 2] = speed;
	int flight = 0, support = 0, contacts = 0;
	V3 previous[2] = {{0}}; bool flatBefore[2] = {0};
	float dt = 1.0f / fps;
	for (int k = 0; k < fps * 3; k++) {
		G.time += dt; G.p.p[wall ? 0 : 2] += speed * dt;
		render();
		if (!an.planted[0] && !an.planted[1]) flight++; else support++;
		int base = HERO_PARTS[HP_TORSO].ni + HERO_PARTS[HP_HEAD].ni;
		int limb = 0; for (int part = HP_UPPERARM; part < HP_COUNT; part++) limb += HERO_PARTS[part].ni;
		for (int s = 0; s < 2; s++) {
			int offset = base + s * limb + HERO_PARTS[HP_THIGH].ni + HERO_PARTS[HP_SHIN].ni;
			Vtx v = dm[D_HERO].v[offset]; V3 pos = v3(v.x,v.y,v.z);
			// Flat support: the rendered shoe's first vertex must stay fixed as the player advances.
			float run = ease((speed-2.2f)/3.8f), amp = ease(speed/.9f);
			float hz = lerpf(fmaxf(.1f,speed/(SC*.95f)),1.25f+speed*.085f,run);
			GaitFoot q = gait_foot(an.gait+s*.5f,speed,hz,run,amp);
			bool flat = q.stance && fabsf(q.pitch) < 1e-6f;
			if (flat && flatBefore[s]) { float slip = vlen(sub(pos,previous[s])); maxSlip=fmaxf(maxSlip,slip); if (slip>.0001f) failures++; contacts++; }
			flatBefore[s] = flat; previous[s] = pos;
		}
		memcpy(copy,dm[D_HERO].v,expected*sizeof(Vtx)); render();
		if (memcmp(copy,dm[D_HERO].v,expected*sizeof(Vtx))) failures++;
	}
	if (speed < 2.2f && flight) failures++;
	if (speed > 6 && (!flight || !support)) failures++;
	printf("%s %.1f at %d fps: support %d flight %d flat contacts %d\n",wall?"wall":"ground",speed,fps,support,flight,contacts);
}
static void transitions(int fps)
{
	memset(&G,0,sizeof G); memset(&an,0,sizeof an); G.p.state=ST_GROUND;
	int before=failures; float dt=1.0f/fps;
	for (int k=0;k<fps*6;k++) {
		float t=k*dt, speed=t<1 ? 2*t : t<2 ? 2 : t<3 ? 2+13*(t-2) : t<4 ? 15 : t<5 ? 15*(5-t) : 0;
		float heading=t<3 ? 0 : (t-3)*.35f;
		G.p.face=PI_F-heading; G.p.v[0]=speed*sinf(heading); G.p.v[2]=speed*cosf(heading);
		G.p.p[0]+=G.p.v[0]*dt; G.p.p[2]+=G.p.v[2]*dt; G.time+=dt;
		if (t>5.5f) G.p.charge=(t-5.5f)*1.8f;
		render();
	}
	printf("start/walk/sprint/turn/stop/crouch at %d fps: %d failures\n",fps,failures-before);
}
static void poses(void)
{
	int before=failures;
	for (int mode=0;mode<10;mode++) {
		memset(&G,0,sizeof G); memset(&an,0,sizeof an);
		G.p.p[1]=30; G.p.state=mode==0?ST_WALL:mode==1?ST_GROUND:ST_AIR;
		G.p.wn[1]=-1; G.p.face=PI_F/2; G.p.v[0]=12;
		for (int k=0;k<60;k++) {
			G.time+=1.0f/60; G.p.phase+=.12f;
			G.p.rope=mode==2; G.p.avis[0]=20; G.p.avis[1]=60;
			G.p.wing=mode==3; G.p.v[1]=mode==4?-30:0;
			G.p.rollT=mode==1?ROLL_DUR*(1-k/60.0f):0;
			G.p.trickType=mode>=5?mode-4:0; G.p.trickT=mode>=5?TRICK_DUR*(1-k/60.0f):0;
			render();
		}
	}
	printf("climb/roll/swing/wing/dive/five tricks: %d failures\n",failures-before);
}
int main(void)
{
	for (int p = 0; p < HP_COUNT; p++) expected += HERO_PARTS[p].ni * (p < HP_UPPERARM ? 1 : 2);
	for (int fps = 30; fps <= 120; fps *= 2) {
		for (int i=0;i<5;i++) locomotion((float[]){.5f,2,5,8.5f,15}[i],fps,false);
		locomotion(19,fps,true);
		transitions(fps);
	}
	poses();
	printf("max rendered contact drift %.7f, max bone error %.5f; %d failures\n",maxSlip,maxBoneError,failures);
	return failures ? 1 : 0;
}
