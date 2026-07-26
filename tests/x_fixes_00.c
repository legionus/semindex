// --------------------------------------------------------
#strong_define	SD	SD_strong
#define		SD	SD_define
int sd_d =	SD;
#undef		SD
int sd_u =	SD;

#strong_undef	SU
#define	SU
#ifdef	SU
int unexpected;
#endif
#define	SU	1
#ifdef	SU
int unexpected;
#endif

// --------------------------------------------------------
#define READ_ONCE(x) x
#define WRITE_ONCE(x, y) x = y

int R, W;

void test_pp_pos(void)
{
  WRITE_ONCE(
     W,
     READ_ONCE(R)
  );
}

// --------------------------------------------------------
static inline void i_func1(void)
{
	unknown();
}
static inline void *i_func2(void)
{
	return i_func1;
}

void test_inline(void)
{
	i_func2();
}

// --------------------------------------------------------
enum E_1 { X_1, Y_1, } e_1;

enum E_2 { X_2, Y_2 };
typeof(Y_2) e_2 = X_2;

enum { X_3, Y_3 } e_3;

enum { X_4, Y_4 };
typeof(X_4) e_4;

// FIXME! see EXPR_VALUE check in primary_expression() to avoid the crash
// sparse errors are correct.
enum { X_5 = bad };
typeof(X_5) e_4;

// --------------------------------------------------------
inline void i_func3(void);
void caller1(void) { i_func3(); }
inline void i_func3(void) { FUNC3(); }

static inline void i_func4(void) { FUNC4(); }
static inline void i_func4(void);
void caller2(void) { i_func4(); }

static void s_func1(void) { FUNC5(); }
static void s_func1(void);
void caller3(void) { s_func1(); }

static void s_func2(void);
void caller4(void) { s_func2(); }
static void s_func2(void) { FUNC6(); }

// --------------------------------------------------------
// sparse/dissect: fix parsing of array designated initializers

struct AO {
 struct AI { int iary[1]; } oary[1][1];
} av = {
 .oary[0][0].iary[0] = av.oary[0][0].iary[0],
};

// --------------------------------------------------------
// sparse/dissect: fix missing definitions for unnamed members of named types

union FMS_U { int m1, m2; };

struct FMS_S {
	union FMS_U;
	int m3;
};

struct FMS_O {
	struct FMS_S;
} fms_o = { .m3 = 0, .m1 = 0 };

union FMS_U fms_u = { .m1 = 0 };

// Avoid unnecessary def's,
// base->ident == sym->ident helps due to deanon
struct NO_FMS_S {
	union { int m; };
} no_fms_s = { .m = 0, };
