#ifndef BENVDBG_H_INCLUDED
#define BENVDBG_H_INCLUDED 1

/* A more general debug statement. _a is any bit of code */
#ifdef DO_DEBUG
#define DBGCALL(_a) do { _a; } while(0)
#else
#define DBGCALL(_a) do {;} while(0)
#endif

/* A standardized approach to including debugging with cvar-style control
   Usage:
    CVARDBG(name,detail,string)
    CVARDBGV(name,detail,formatstring-and-args)
   where
    name is the cvar name
    detail is one of BASIC,DETAIL,ALL
 */
#define _CVAR_VERBOSITY_BASIC 1
#define _CVAR_VERBOSITY_DETAIL 2
#define _CVAR_VERBOSITY_ALL 3

/* This allows selecting all ranks or 1 rank for output */
extern FILE *cvar_vfp;
extern int cvar_benv_rank, cvar_benv_wrank;
/* Do we need a different default value? */
/* This is a cvar local to a file */
#define CDBGDECL(_class) \
    static int cvar_benv_##_class##_verbose = 0;\
    static int cvar_##_class##_indent = 0
/* This is a global version. These should be used sparingly because the
   names are globally visible */
#define CDBGGDECL(_class) \
    int cvar_benv_##_class##_verbose = 0;\
    int cvar_##_class##_indent = 0
/* And the external declaration */
#define CDBGEDECL(_class) \
    extern int cvar_benv_##_class##_verbose;\
    extern int cvar_##_class##_indent

#define CDBGV(_class,_detail,...) do {\
    if (cvar_benv_##_class##_verbose >= _CVAR_VERBOSITY_##_detail && \
	(cvar_benv_rank == -1 || cvar_benv_rank == cvar_benv_wrank)) {	\
    if (!cvar_vfp) cvar_vfp = stderr;\
    for (int _i=0; _i<cvar_##_class##_indent;_i++) fputc(' ',cvar_vfp);\
    fprintf(cvar_vfp,__VA_ARGS__); fflush(cvar_vfp);}} while(0)
#define CDBG(_class,_detail,_str) CDBGV(_class,_detail,"%s\n",_str)
#define CDBGCMD(_class,_detail,_cmd) do {\
    if (cvar_benv_##_class##_verbose >= _CVAR_VERBOSITY_##_detail && \
	(cvar_benv_rank == -1 || cvar_benv_rank == cvar_benv_wrank)) {	\
	_cmd;}} while(0)
#define CDBGPUSH(_class) \
    cvar_##_class##_indent++
#define CDBGPOP(_class) \
    cvar_##_class##_indent--
#define CDBGGETVAL(_class) cvar_benv_##_class##_verbose
#define CDBGSETVAL(_class,_val) cvar_benv_##_class##_verbose = _val
#define CDBGINCRVAL(_class) cvar_benv_##_class##_verbose++

/* Function trace calls */
#if 1
#define CDBGFCALLDECL extern int cvar_benv_funccall, cvar_benv_funccall_indent
#define CDBGFCALLENTER do { if (cvar_benv_funccall) { \
    for (int _i=0; _i<cvar_benv_funccall_indent; _i++) fputc(' ',cvar_vfp);\
    fprintf(cvar_vfp,"Entering %s\n",__func__);\
    cvar_benv_funccall_indent++;} }while(0)
#define CDBGFCALLENTERV(_fmt,...) do { if (cvar_benv_funccall) { \
    for (int _i=0; _i<cvar_benv_funccall_indent; _i++) fputc(' ',cvar_vfp);\
    fprintf(cvar_vfp,"Entering %s: ",__func__);\
    fprintf(cvar_vfp, _fmt, __VA_ARGS__);\
    cvar_benv_funccall_indent++;} }while(0)
#define CDBGFCALLEXIT do { if (cvar_benv_funccall) { \
    cvar_benv_funccall_indent--;\
    for (int _i=0; _i<cvar_benv_funccall_indent; _i++) fputc(' ',cvar_vfp);\
    fprintf(cvar_vfp,"Exiting %s\n",__func__);} }while(0)
#define CDBGFCALLEXITV(_fmt,...) do { if (cvar_benv_funccall) {	\
    cvar_benv_funccall_indent--;\
    for (int _i=0; _i<cvar_benv_funccall_indent; _i++) fputc(' ',cvar_vfp);\
    fprintf(cvar_vfp,"Exiting %s: ",__func__);\
    fprintf(cvar_vfp, _fmt, __VA_ARGS__);\
    } }while(0)
#else
#define CDBGFCALLDECL  extern int cvar_benv_funccall, cvar_benv_funccall_indent
#define CDBGFCALLENTER do {} while (0)
#define CDBGFCALLEXIT  do {} while (0)
#endif

#endif
