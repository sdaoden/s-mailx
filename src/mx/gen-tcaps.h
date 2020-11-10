/*@ gen-tcaps.h, generated by make-tcap-map.pl.
 *@ See termcap.c for more */

static char const a_termcap_namedat[] = {
	/* [0]+7, mx_TERMCAP_CMD_ks */ 'k','s',  's','m','k','x','\0',
	/* [7]+7, mx_TERMCAP_CMD_ke */ 'k','e',  'r','m','k','x','\0',
	/* [14]+8, mx_TERMCAP_CMD_te */ 't','e',  'r','m','c','u','p','\0',
	/* [22]+8, mx_TERMCAP_CMD_ti */ 't','i',  's','m','c','u','p','\0',
	/* [30]+5, mx_TERMCAP_CMD_BE */ '\0','\0',  'B','E','\0',
	/* [35]+5, mx_TERMCAP_CMD_BD */ '\0','\0',  'B','D','\0',
	/* [40]+5, mx_TERMCAP_CMD_ce */ 'c','e',  'e','l','\0',
	/* [45]+6, mx_TERMCAP_CMD_ch */ 'c','h',  'h','p','a','\0',
	/* [51]+5, mx_TERMCAP_CMD_cr */ 'c','r',  'c','r','\0',
	/* [56]+7, mx_TERMCAP_CMD_le */ 'l','e',  'c','u','b','1','\0',
	/* [63]+7, mx_TERMCAP_CMD_nd */ 'n','d',  'c','u','f','1','\0',
	/* [70]+8, mx_TERMCAP_CMD_cl */ 'c','l',  'c','l','e','a','r','\0',
	/* [78]+5, mx_TERMCAP_CMD_cd */ 'c','d',  'e','d','\0',
	/* [83]+7, mx_TERMCAP_CMD_ho */ 'h','o',  'h','o','m','e','\0',
	/* [90]+5, mx_TERMCAP_QUERY_am */ 'a','m',  'a','m','\0',
	/* [95]+6, mx_TERMCAP_QUERY_sam */ 'Y','E',  's','a','m','\0',
	/* [101]+7, mx_TERMCAP_QUERY_xenl */ 'x','n',  'x','e','n','l','\0',
	/* [108]+5, mx_TERMCAP_QUERY_PS */ '\0','\0',  'P','S','\0',
	/* [113]+5, mx_TERMCAP_QUERY_PE */ '\0','\0',  'P','E','\0',
	/* [118]+9, mx_TERMCAP_QUERY_colors */ 'C','o',  'c','o','l','o','r','s','\0',
#ifdef mx_HAVE_KEY_BINDINGS
	/* [127]+6, mx_TERMCAP_QUERY_key_backspace */ 'k','b',  'k','b','s','\0',
	/* [133]+8, mx_TERMCAP_QUERY_key_dc */ 'k','D',  'k','d','c','h','1','\0',
	/* [141]+6, mx_TERMCAP_QUERY_key_sdc */ '*','4',  'k','D','C','\0',
	/* [147]+6, mx_TERMCAP_QUERY_key_eol */ 'k','E',  'k','e','l','\0',
	/* [153]+7, mx_TERMCAP_QUERY_key_exit */ '@','9',  'k','e','x','t','\0',
	/* [160]+8, mx_TERMCAP_QUERY_key_ic */ 'k','I',  'k','i','c','h','1','\0',
	/* [168]+6, mx_TERMCAP_QUERY_key_sic */ '#','3',  'k','I','C','\0',
	/* [174]+8, mx_TERMCAP_QUERY_key_home */ 'k','h',  'k','h','o','m','e','\0',
	/* [182]+7, mx_TERMCAP_QUERY_key_shome */ '#','2',  'k','H','O','M','\0',
	/* [189]+7, mx_TERMCAP_QUERY_key_end */ '@','7',  'k','e','n','d','\0',
	/* [196]+7, mx_TERMCAP_QUERY_key_send */ '*','7',  'k','E','N','D','\0',
	/* [203]+6, mx_TERMCAP_QUERY_key_npage */ 'k','N',  'k','n','p','\0',
	/* [209]+6, mx_TERMCAP_QUERY_key_ppage */ 'k','P',  'k','p','p','\0',
	/* [215]+8, mx_TERMCAP_QUERY_key_left */ 'k','l',  'k','c','u','b','1','\0',
	/* [223]+7, mx_TERMCAP_QUERY_key_sleft */ '#','4',  'k','L','F','T','\0',
	/* [230]+8, mx_TERMCAP_QUERY_xkey_aleft */ '\0','\0',  'k','L','F','T','3','\0',
	/* [238]+8, mx_TERMCAP_QUERY_xkey_cleft */ '\0','\0',  'k','L','F','T','5','\0',
	/* [246]+8, mx_TERMCAP_QUERY_key_right */ 'k','r',  'k','c','u','f','1','\0',
	/* [254]+7, mx_TERMCAP_QUERY_key_sright */ '%','i',  'k','R','I','T','\0',
	/* [261]+8, mx_TERMCAP_QUERY_xkey_aright */ '\0','\0',  'k','R','I','T','3','\0',
	/* [269]+8, mx_TERMCAP_QUERY_xkey_cright */ '\0','\0',  'k','R','I','T','5','\0',
	/* [277]+8, mx_TERMCAP_QUERY_key_down */ 'k','d',  'k','c','u','d','1','\0',
	/* [285]+6, mx_TERMCAP_QUERY_xkey_sdown */ '\0','\0',  'k','D','N','\0',
	/* [291]+7, mx_TERMCAP_QUERY_xkey_adown */ '\0','\0',  'k','D','N','3','\0',
	/* [298]+7, mx_TERMCAP_QUERY_xkey_cdown */ '\0','\0',  'k','D','N','5','\0',
	/* [305]+8, mx_TERMCAP_QUERY_key_up */ 'k','u',  'k','c','u','u','1','\0',
	/* [313]+6, mx_TERMCAP_QUERY_xkey_sup */ '\0','\0',  'k','U','P','\0',
	/* [319]+7, mx_TERMCAP_QUERY_xkey_aup */ '\0','\0',  'k','U','P','3','\0',
	/* [326]+7, mx_TERMCAP_QUERY_xkey_cup */ '\0','\0',  'k','U','P','5','\0',
	/* [333]+6, mx_TERMCAP_QUERY_kf0 */ 'k','0',  'k','f','0','\0',
	/* [339]+6, mx_TERMCAP_QUERY_kf1 */ 'k','1',  'k','f','1','\0',
	/* [345]+6, mx_TERMCAP_QUERY_kf2 */ 'k','2',  'k','f','2','\0',
	/* [351]+6, mx_TERMCAP_QUERY_kf3 */ 'k','3',  'k','f','3','\0',
	/* [357]+6, mx_TERMCAP_QUERY_kf4 */ 'k','4',  'k','f','4','\0',
	/* [363]+6, mx_TERMCAP_QUERY_kf5 */ 'k','5',  'k','f','5','\0',
	/* [369]+6, mx_TERMCAP_QUERY_kf6 */ 'k','6',  'k','f','6','\0',
	/* [375]+6, mx_TERMCAP_QUERY_kf7 */ 'k','7',  'k','f','7','\0',
	/* [381]+6, mx_TERMCAP_QUERY_kf8 */ 'k','8',  'k','f','8','\0',
	/* [387]+6, mx_TERMCAP_QUERY_kf9 */ 'k','9',  'k','f','9','\0',
	/* [393]+7, mx_TERMCAP_QUERY_kf10 */ 'k',';',  'k','f','1','0','\0',
	/* [400]+7, mx_TERMCAP_QUERY_kf11 */ 'F','1',  'k','f','1','1','\0',
	/* [407]+7, mx_TERMCAP_QUERY_kf12 */ 'F','2',  'k','f','1','2','\0',
	/* [414]+7, mx_TERMCAP_QUERY_kf13 */ 'F','3',  'k','f','1','3','\0',
	/* [421]+7, mx_TERMCAP_QUERY_kf14 */ 'F','4',  'k','f','1','4','\0',
	/* [428]+7, mx_TERMCAP_QUERY_kf15 */ 'F','5',  'k','f','1','5','\0',
	/* [435]+7, mx_TERMCAP_QUERY_kf16 */ 'F','6',  'k','f','1','6','\0',
	/* [442]+7, mx_TERMCAP_QUERY_kf17 */ 'F','7',  'k','f','1','7','\0',
	/* [449]+7, mx_TERMCAP_QUERY_kf18 */ 'F','8',  'k','f','1','8','\0',
	/* [456]+7, mx_TERMCAP_QUERY_kf19 */ 'F','9',  'k','f','1','9','\0',
#endif /* mx_HAVE_KEY_BINDINGS */
};

static struct a_termcap_control const a_termcap_control[] = {
	{/* 0. mx_TERMCAP_CMD_ks */ mx_TERMCAP_CAPTYPE_STRING, 0},
	{/* 1. mx_TERMCAP_CMD_ke */ mx_TERMCAP_CAPTYPE_STRING, 7},
	{/* 2. mx_TERMCAP_CMD_te */ mx_TERMCAP_CAPTYPE_STRING, 14},
	{/* 3. mx_TERMCAP_CMD_ti */ mx_TERMCAP_CAPTYPE_STRING, 22},
# ifdef mx_HAVE_KEY_BINDINGS /* for now */
	{/* 4. mx_TERMCAP_CMD_BE */ mx_TERMCAP_CAPTYPE_STRING, 30},
	{/* 5. mx_TERMCAP_CMD_BD */ mx_TERMCAP_CAPTYPE_STRING, 35},
# endif
# ifdef mx_HAVE_MLE
	{/* 6. mx_TERMCAP_CMD_ce */ mx_TERMCAP_CAPTYPE_STRING, 40},
	{/* 7. mx_TERMCAP_CMD_ch */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_ARG_IDX1, 45},
	{/* 8. mx_TERMCAP_CMD_cr */ mx_TERMCAP_CAPTYPE_STRING, 51},
	{/* 9. mx_TERMCAP_CMD_le */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_ARG_CNT, 56},
	{/* 10. mx_TERMCAP_CMD_nd */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_ARG_CNT, 63},
	{/* 11. mx_TERMCAP_CMD_cl */ mx_TERMCAP_CAPTYPE_STRING, 70},
	{/* 12. mx_TERMCAP_CMD_cd */ mx_TERMCAP_CAPTYPE_STRING, 78},
	{/* 13. mx_TERMCAP_CMD_ho */ mx_TERMCAP_CAPTYPE_STRING, 83},
# endif
	{/* 14. mx_TERMCAP_QUERY_am */ mx_TERMCAP_CAPTYPE_BOOL|a_TERMCAP_F_QUERY, 90},
	{/* 15. mx_TERMCAP_QUERY_sam */ mx_TERMCAP_CAPTYPE_BOOL|a_TERMCAP_F_QUERY, 95},
	{/* 16. mx_TERMCAP_QUERY_xenl */ mx_TERMCAP_CAPTYPE_BOOL|a_TERMCAP_F_QUERY, 101},
# ifdef mx_HAVE_KEY_BINDINGS /* for now */
	{/* 17. mx_TERMCAP_QUERY_PS */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 108},
	{/* 18. mx_TERMCAP_QUERY_PE */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 113},
# endif
# ifdef mx_HAVE_COLOUR
	{/* 19. mx_TERMCAP_QUERY_colors */ mx_TERMCAP_CAPTYPE_NUMERIC|a_TERMCAP_F_QUERY, 118},
# endif
# ifdef mx_HAVE_KEY_BINDINGS
	{/* 20. mx_TERMCAP_QUERY_key_backspace */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 127},
	{/* 21. mx_TERMCAP_QUERY_key_dc */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 133},
	{/* 22. mx_TERMCAP_QUERY_key_sdc */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 141},
	{/* 23. mx_TERMCAP_QUERY_key_eol */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 147},
	{/* 24. mx_TERMCAP_QUERY_key_exit */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 153},
	{/* 25. mx_TERMCAP_QUERY_key_ic */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 160},
	{/* 26. mx_TERMCAP_QUERY_key_sic */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 168},
	{/* 27. mx_TERMCAP_QUERY_key_home */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 174},
	{/* 28. mx_TERMCAP_QUERY_key_shome */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 182},
	{/* 29. mx_TERMCAP_QUERY_key_end */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 189},
	{/* 30. mx_TERMCAP_QUERY_key_send */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 196},
	{/* 31. mx_TERMCAP_QUERY_key_npage */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 203},
	{/* 32. mx_TERMCAP_QUERY_key_ppage */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 209},
	{/* 33. mx_TERMCAP_QUERY_key_left */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 215},
	{/* 34. mx_TERMCAP_QUERY_key_sleft */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 223},
	{/* 35. mx_TERMCAP_QUERY_xkey_aleft */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 230},
	{/* 36. mx_TERMCAP_QUERY_xkey_cleft */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 238},
	{/* 37. mx_TERMCAP_QUERY_key_right */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 246},
	{/* 38. mx_TERMCAP_QUERY_key_sright */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 254},
	{/* 39. mx_TERMCAP_QUERY_xkey_aright */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 261},
	{/* 40. mx_TERMCAP_QUERY_xkey_cright */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 269},
	{/* 41. mx_TERMCAP_QUERY_key_down */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 277},
	{/* 42. mx_TERMCAP_QUERY_xkey_sdown */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 285},
	{/* 43. mx_TERMCAP_QUERY_xkey_adown */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 291},
	{/* 44. mx_TERMCAP_QUERY_xkey_cdown */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 298},
	{/* 45. mx_TERMCAP_QUERY_key_up */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 305},
	{/* 46. mx_TERMCAP_QUERY_xkey_sup */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 313},
	{/* 47. mx_TERMCAP_QUERY_xkey_aup */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 319},
	{/* 48. mx_TERMCAP_QUERY_xkey_cup */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 326},
	{/* 49. mx_TERMCAP_QUERY_kf0 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 333},
	{/* 50. mx_TERMCAP_QUERY_kf1 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 339},
	{/* 51. mx_TERMCAP_QUERY_kf2 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 345},
	{/* 52. mx_TERMCAP_QUERY_kf3 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 351},
	{/* 53. mx_TERMCAP_QUERY_kf4 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 357},
	{/* 54. mx_TERMCAP_QUERY_kf5 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 363},
	{/* 55. mx_TERMCAP_QUERY_kf6 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 369},
	{/* 56. mx_TERMCAP_QUERY_kf7 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 375},
	{/* 57. mx_TERMCAP_QUERY_kf8 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 381},
	{/* 58. mx_TERMCAP_QUERY_kf9 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 387},
	{/* 59. mx_TERMCAP_QUERY_kf10 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 393},
	{/* 60. mx_TERMCAP_QUERY_kf11 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 400},
	{/* 61. mx_TERMCAP_QUERY_kf12 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 407},
	{/* 62. mx_TERMCAP_QUERY_kf13 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 414},
	{/* 63. mx_TERMCAP_QUERY_kf14 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 421},
	{/* 64. mx_TERMCAP_QUERY_kf15 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 428},
	{/* 65. mx_TERMCAP_QUERY_kf16 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 435},
	{/* 66. mx_TERMCAP_QUERY_kf17 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 442},
	{/* 67. mx_TERMCAP_QUERY_kf18 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 449},
	{/* 68. mx_TERMCAP_QUERY_kf19 */ mx_TERMCAP_CAPTYPE_STRING|a_TERMCAP_F_QUERY, 456},
# endif /* mx_HAVE_KEY_BINDINGS */
};
