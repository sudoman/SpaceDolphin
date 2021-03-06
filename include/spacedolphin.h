/*  Copyright 2011 Andrew Engelbrecht <sudoman@ninthfloor.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <search.h>
#include <SDL/SDL.h>
#include <cairo/cairo.h>
#include <chipmunk.h>

enum players { P_NONE, P_ONE, P_TWO, P_THREE, P_FOUR };
enum shape { S_NONE, S_LSEG, S_CIRC, S_POLY };
enum collidetype { C_NONE, C_SHIP, C_LARGE, C_SMALL };
enum colors { COLOR_LARGE, COLOR_SMALL, COLOR_LINE, COLOR_SHIP,
    COLOR_BHOLE, COLOR_NONE
};
enum sizes { HSIZE = sizeof(enum players) * sizeof(enum colors) * 2 };


struct color_rgba {
    float r, g, b, a;
};

// contains two colors, one for fill, the other for border
struct colorset {
    struct color_rgba c1;
    struct color_rgba c2;
};

// struct of force and torque vectors
struct forces {
    cpVect force;
    cpVect tforce;
};

// remembers which key presses/forces are active for a player
struct movement {
    bool up;
    bool down;
    bool left;
    bool right;
    bool cw;
    bool ccw;
    struct forces prevf;
};

// basic player information
struct playerinfo {
    char *name;
    int hp;
    struct movement thrust;
};

// linked list node to keep track of objects for drawing and accessing
struct objnode {
    int geom;
    cpFloat radius;
    cpBody *b;
    cpShape *s;
    bool bhole;
    struct playerinfo *pinfo;
    int ownedby;
    int colortype;
    struct timespec lastchange;
    struct objnode *lastchangeby;
    struct objnode *prev;
    struct objnode *next;
};

#define HPSTART	       16	// the hitpoints players start with
#define SHORTTIME     0.1	// max sec between collisions to be "continuous"

#define DEBUG	    false	// print info about graphics mode
#define SHOWFPS	     true	// show the fps if true
#define FULLSCREEN  false	// is fullscreen mode the default?

#define DT       5e5		// 5e5 nanoseconds: phys engine time step size
#define MAXFPS   60		// max frames per second
#define MINFT    ((long) 1e9 / MAXFPS)	// min frame time
#define MINFPS   20		// min frames per second
#define MAXFT    ((long) 1e9 / MINFPS)	// max frame time
#define MINIDLEP 5		// minimum % of cpu to leave idle
#define NITER    MAXFPS		// n of frames to average, to calc actual fps

#define FORCE       200.0	// force of rocket's jetpack
#define TFORCE       75.0	// proportional to torque of rocket
#define RLEN          4.0	// length of radius at which TFORCE is applied
#define MAXVEL	    200.0	// soft limit for velocity
#define MAXANGVEL     8.0	// soft limit for angular velocity
#define TORQRAMPT     1e9	// ns over which torque ramps up while turning

//#define VGRAV     -50.0       // upwards gravity (so negative means down)
#define VGRAV	      0.0	// upwards gravity (so negative means down)
#define BGRAV     64000.0	// gravity towards any black holes
#define RDSQ	     10.0	// squared distance from BH to apply rep. force
#define REPFS	    400.0	// scaled strength of replellant force

#define XMAX        160.0	// the width of the space
#define YMAX        120.0	// the height of the space
#define XYBUF         5.0	// border not to put rand objects in
#define HPBUF	      4.0	// space to give for the hp meters

#define SCALEF	      4.0	// scaling factor for choosing window size
#define LPIXW	      1.0	// the pixel width of lines and borders
#define FONTSIZE       14	// font size (pixel height?)

#define PI   3.1415926535	// just pi

// draw.c
void graphicsinit(SDL_Surface ** screen, SDL_Surface ** sdlbuff,
		  cairo_surface_t ** surface, cairo_t ** cr);
void drawshapes(SDL_Surface * screen, SDL_Surface * sdlbuff, cairo_t * cr,
		struct objnode *objroot, long simtime);
void showwinner(SDL_Surface * screen, SDL_Surface * sdlbuff, cairo_t * cr,
		struct objnode *objroot, long simtime);
SDL_Surface *togglefullscreen(void);

// move.c
bool interact(cpSpace * space, struct objnode *objroot,
	      SDL_Surface ** screen);
void dontfall(cpBody * body, cpVect gravity, cpFloat damping, cpFloat dt);
void orbit(cpBody * body, cpVect gravity, cpFloat damping, cpFloat dt);
struct objnode *findplayer(struct objnode *objroot, int playernum);

// time.c
void framerate(long simtime, double *simrate, int *fps);
long timebal(void);
void waitns(long ns);
struct timespec convttp(long ns);
long convtns(struct timespec tp);
struct timespec curtime(void);
struct timespec tdiff(struct timespec tp0, struct timespec tp1);
bool isbrief(struct timespec dt);

// shape.c
cpSpace *makeshapes(struct objnode *objx);
void rmobj(struct objnode *objx);
void rmobjs(struct objnode *objroot);

// collide.c
int chcolor(cpArbiter * arb, cpSpace * space, void *data);

// color.c
void initcolors(void);
struct colorset *findcolors(int colortype, int ownedby);
void freecolorentries(void);
