/*@ src/mx/gen-cmd-tab.h, generated by mk/make-cmd-tab.sh.
 *@ See cmd.c and cmd-tab.h for more */

#define a_CMD_DEFAULT_IDX 94
#define a_CMD_COUNT 203
static u8 const a_cmd__cidx[128] = {
#undef a_X
#define a_X a_CMD_COUNT
   /* 0x0 */ a_X, a_X, a_X, a_X,
   /* 0x4 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x9 */ a_X, a_X, a_X, a_X, a_X,
   /* 0xE */ a_X, a_X, a_X, a_X, a_X,
   /* 0x13 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x18 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x1D */ a_X, a_X, a_X, a_X, 0,
   /* 0x22 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x27 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x2C */ a_X, a_X, a_X, a_X, a_X,
   /* 0x31 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x36 */ a_X, a_X, a_X, a_X, a_X,
   /* 0x3B */ a_X, a_X, 1, a_X, 2,
   /* 0x40 */ a_X, a_X, a_X, 21, 27,
   /* 0x45 */ a_X, 47, a_X, a_X, a_X,
   /* 0x4A */ a_X, a_X, 75, 79, 96,
   /* 0x4F */ a_X, 100, a_X, 115, 126,
   /* 0x54 */ 152, 179, a_X, a_X, a_X,
   /* 0x59 */ a_X, 201, a_X, a_X, a_X,
   /* 0x5E */ a_X, a_X, a_X, 4, 9,
   /* 0x63 */ 10, 25, 36, 48, 64,
   /* 0x68 */ 66, 71, a_X, a_X, 76,
   /* 0x6D */ 80, 94, a_X, 101, 107,
   /* 0x72 */ 108, 127, 153, 159, 192,
   /* 0x77 */ 198, 199, a_X, 202, a_X,
   /* 0x7C */ 3, a_X, a_X, a_X,
#undef a_X
};
#define a_CMD_CIDX(C) (S(u8,C) <= 127 ? a_cmd__cidx[S(u8,C)] : a_CMD_COUNT)
CTAV(a_CMD_COUNT == NELEM(a_cmd_ctable));
