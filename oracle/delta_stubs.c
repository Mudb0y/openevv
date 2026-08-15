/* What the Delta runtime needs from the layers it does not contain.
 *
 * The runtime links on its own, but runtime_new reaches for the entry points
 * of everything around it: the per-language generated file, the dlang stream
 * layer, the eloqc front end, the dictionaries and the error callback. None
 * of those is on the path of an individual primitive, so the harness stubs
 * them rather than dragging a whole language module into the binary.
 *
 * Anything a primitive under test actually calls must come out of this file
 * and be linked for real, or the comparison is meaningless.
 */

void delta_delete(void *d)                 { (void)d; }
int  dlang_new(void *d)                    { (void)d; return 0; }
void dlang_delete(void *d)                 { (void)d; }
int  eloqc_new(void *d)                    { (void)d; return 0; }
void eloqc_delete(void *d)                 { (void)d; }
int  DeltaProc_main(void *d)               { (void)d; return 0; }
int  ins_tokens(void *d, int f, const void *s, int n, int a)
{
    (void)d; (void)f; (void)s; (void)n; (void)a;
    return 0;
}
int  ins_rdtoks(void *d)                   { (void)d; return 0; }
int  actdlookup(void *d)                   { (void)d; return 0; }
int  setdlookup(void *d, int a, int b, void *c, int e)
{
    (void)d; (void)a; (void)b; (void)c; (void)e;
    return 0;
}
int  vdictinit(void *d)                    { (void)d; return 0; }
int  vlinkinit(void *d)                    { (void)d; return 0; }
int  errorIgnore(void *a, void *b)         { (void)a; (void)b; return 0; }
void dtSetErrorCallback(void *a, void *b)  { (void)a; (void)b; }
