#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dir.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include "windows.h"

// idk why i made any of these macros now that i think about it, maybe to get rid of magic numbers,
// but there's still a ton of magic numbers everywhere

#define OFFSET 0xC
#define CHUNKSIZE 65536
#define BYTE 256
#define MAX 200
#define PI 3.1415926535

// more dumb things
#define C2_NEIGHBOURS_END 0x1B4
#define C2_NEIGHBOURS_FLAGS_END 0x1D4
#define MAGIC_ENTRY 0x100FFFF
#define MAGIC_TEXTURE 0x11234
#define C2_MUSIC_REF 0x2A4
#define NONE 0x6396347Fu

// commands
#define KILL 2089250961u
#define HELP 2089138798u
#define EXPORT 2939723207u
#define EXPORTALL 1522383616u
#define CHANGEPRINT 2239644728u
#define CHANGEMODE 588358864u
#define STATUS 3482341513u
#define WIPE 2089682330u
#define IMPORT 3083219648u
#define INTRO 223621809u
#define RESIZE 3426052343u
#define ROTATE 3437938580u
#define BUILD 215559733u
#define PROP 2089440550u

#define ENTRY_TYPE_ANIM 0x1
#define ENTRY_TYPE_ZONE 0x7
#define ENTRY_TYPE_GOOL 0xB
#define ENTRY_TYPE_INST 0xE
#define ENTRY_TYPE_VCOL 0xF
#define ENTRY_TYPE_DEMO 0x13
#define ENTRY_TYPE_SOUND 0xC

#define ENTITY_PROP_NAME                0x2C
#define ENTITY_PROP_DEPTH_MODIFIER      0x32
#define ENTITY_PROP_PATH                0x4B
#define ENTITY_PROP_ID                  0x9F
#define ENTITY_PROP_ARGUMENTS           0xA4
#define ENTITY_PROP_TYPE                0xA9
#define ENTITY_PROP_SUBTYPE             0xAA
#define ENTITY_PROP_VICTIMS             0x287
#define ENTITY_PROP_BOX_COUNT           28B

#define ENTITY_PROP_CAMERA_MODE         0x29
#define ENTITY_PROP_CAM_AVG_PT_DIST     0xC9
#define ENTITY_PROP_CAM_SLST            0x103
#define ENTITY_PROP_CAM_PATH_LINKS      0x109
#define ENTITY_PROP_CAM_FOV             0x130
#define ENTITY_PROP_CAM_DRAW_LIST_A     0x13B
#define ENTITY_PROP_CAM_DRAW_LIST_B     0x13C
#define ENTITY_PROP_CAM_INDEX           0x173
#define ENTITY_PROP_CAM_SUBINDEX        0x174
#define ENTITY_PROP_CAM_LINK_COUNT      0x176
#define ENTITY_PROP_CAM_LOAD_LIST_A     0x208
#define ENTITY_PROP_CAM_LOAD_LIST_B     0x209

#define ENTITY_PROP_CAM_DISTANCE        0x142
#define ENTITY_PROP_CAM_WARP_SWITCH     0x1A8
#define ENTITY_PROP_CAM_BG_COLORS       0x1FA
#define ENTITY_PROP_CAM_UPDATE_SCENERY  0x27F


typedef struct info{
    int counter[22];    // counts entry types, counter[0] is total entry count
    int print_en;   // variable for storing printing status 0 - nowhere, 1 - screen, 2 - file, 3 - both
    FILE *flog;  // file where the logs are exported if file print is enabled
    char temp[MAX]; // strings are written into this before being printed by condprint
    int gamemode;   // 2 for C2, 3 for C3
    int portmode;   // 0 for normal export, 1 for porting
    unsigned int anim[1024][3];  // field one is model, field 3 is animation, field 2 is original animation when c3->c2
    unsigned int animrefcount;   // count of animation references when porting c3 to c2
} INFO;

typedef struct spawn{
    int x;
    int y;
    int z;
    unsigned int zone;
} SPAWN;

typedef struct spawns{
    int spawn_count;
    SPAWN *spawns;
} SPAWNS;

typedef struct entry{
    unsigned int EID;
    int esize;
    int chunk;
    unsigned char *data;
    unsigned int *related = NULL;
} ENTRY;

typedef struct item {
    int eid;
    int index;
} ITEM;

typedef struct payload {
    int *chunks;
    int count;
    unsigned int zone;
} PAYLOAD;

typedef struct payloads {
    int count;
    PAYLOAD *arr;
} PAYLOADS;

typedef struct list {
    int count;
    unsigned int *eids;
} LIST;

typedef struct load {
    char type;
    int list_length;
    unsigned int *list;
    int index;
} LOAD;
typedef struct load_list {
    int count;
    LOAD array[1000];
} LOAD_LIST;

typedef struct relation {
    int value;
    int index1;
    int index2;
} RELATION;

typedef struct relations {
    int count;
    RELATION *relations;
} RELATIONS;

typedef struct inf {
    int type;
    int subtype;
    LIST dependencies;
} INF;

typedef struct {
    int count;
    INF *array;
} DEPENDENCIES;

//functional prototypes, also list of functions excluding main and main1, definitely outdated
void rot(unsigned int *x,unsigned int *y, double rotation);
void rotate_zone(unsigned char *buffer, char *filepath, double rotation);
void rotate_scenery(unsigned char *buffer, char *filepath, double rotation, char *time, int filesize);
void rotate_main(char *time);
void make_path(char *finalpath, char *type, int eid, char *lvlid, char *date, INFO status);
void resize_chunk_handler(unsigned char *chunk, INFO status, double scale[3]);
void resize_entity(unsigned char *item, int itemsize, double scale[3], INFO status);
void resize_zone(int fsize, unsigned char *buffer, double scale[3], INFO status);
void resize_level(FILE *fpath, char *path, double scale[3], char *time, INFO status);
void resize_folder(DIR *df, char *path, double scale[3], char *time, INFO status);
void resize_scenery(int fsize, unsigned char *buffer, double scale[3], INFO status);
void resize_main(char *time, INFO status);
void chunksave(unsigned char *chunk, int *index, int *curr_off, int *curr_chunk, FILE *fnew, int offsets[]);
int camfix(unsigned char *cam, int length);
void entitycoordfix(unsigned char *item, int itemlength);
unsigned int from_u32(unsigned char *data);
unsigned int from_u16(unsigned char *data);
void scenery(unsigned char *buffer, int entrysize,char *lvlid, char *date);
unsigned int nsfChecksum(const unsigned char *data);
int filelister(char *path, FILE *base);
int import(char *time, INFO status);
void printstatus(INFO status);
void clrscr();
void askmode(int *zonetype, INFO *status);
void condprint(INFO status);
void askprint(INFO *status);
void generic_entry(unsigned char *buffer, int entrysize,char *lvlid, char *date);
void gool(unsigned char *buffer, int entrysize,char *lvlid, char *date);
void zone(unsigned char *buffer, int entrysize,char *lvlid, char *date, int zonetype);
void model(unsigned char *buffer, int entrysize,char *lvlid, char *date);
void countwipe(INFO *status);
void countprint(INFO status);
void animation(unsigned char *buffer, int entrysize,char *lvlid, char *date);
void pathstring(char *finalpath, char *type, int eid, char *lvlid, char *date);
char* eid_conv(unsigned int m_value, char *eid);
int normal_chunk(unsigned char *buffer, char *lvlid, char *date, int zonetype);
int texture_chunk(unsigned char *buffer, char *lvlid, char *date);
int sound_chunk(unsigned char *buffer);
int wavebank_chunk(unsigned char *buffer);
int chunk_handler(unsigned char *buffer,int chunkid, char *lvlid, char *date, int zonetype);
int exprt(int zone, char *fpath, char *date);
void intro_text();
void print_help();
const unsigned long hash(const char *str);
void build_main(char *nsfpath, char *dirpath, int chunkcap, INFO status, char *time);
unsigned int* getrelatives(unsigned char *entry);
unsigned int get_slst(unsigned char *item);
unsigned int* GOOL_relatives(unsigned char *entry);
void prop_main(char* path);
long long int fact(int n);
int cmpfunc(const void *a, const void *b);
int comp(const void *a, const void *b);
int pay_cmp(const void *a, const void *b);
int load_list_sort(const void *a, const void *b);
int list_comp(const void *a, const void *b);
void swap_ints(int *a, int *b);
int cmp_entry(const void *a, const void *b);
unsigned int eid_to_int(char *eid);
int cmp_entry_eid(const void *a, const void *b);
int relations_cmp(const void *a, const void *b);
RELATIONS transform_matrix(LIST entries, int **entry_matrix);
int get_index(unsigned int eid, ENTRY *elist, int entry_count);
