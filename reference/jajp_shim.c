/* The two names IBM's Japanese objects want and their own object set does not
 * carry, so that a Japanese reference can be linked at all.
 *
 * This is only for the reference. Neither belongs in our engine: one is a path
 * to files we never read, and the other is a compiler's idea of how to make a
 * large stack frame. It is linked only for TAG=jajp, because every other
 * module defines getFullPathName itself in libmain.obj and would collide.
 *
 * Without these there is no Japanese oracle, and without an oracle nothing
 * about a Japanese build can be held to the standard everything else here is
 * held to. That is the whole reason this file exists.
 */

/* Where the library was loaded from. IBM's own is one line -- it answers a
   global that DllMain fills in -- and that global is a 260-byte buffer in the
   bss, so in a static build with no DllMain it answers a pointer to an empty
   string rather than nothing at all. Every other module's reference gets
   exactly that, because it links IBM's libmain.obj and there is no DllMain
   there either. So this answers the same: an empty path, not a null one.

   Returning nought instead, which is what this did first, is not the same
   thing and is the sort of difference that makes an oracle worth less than no
   oracle. */
static const char evv_no_path[260];

const char *evv_getFullPathName(void)
{
    return evv_no_path;
}
__asm__(".globl \"?getFullPathName@@YAPBDXZ\"\n"
        ".set \"?getFullPathName@@YAPBDXZ\", _evv_getFullPathName\n");

/* Microsoft's compiler calls this instead of subtracting from the stack
   pointer when a frame is large: the size arrives in eax and this does the
   subtraction itself, leaving the return address on the new top. Probing the
   pages on the way down is what the name is about and what this leaves out,
   which costs nothing where the stack is already there. The name carries the
   platform's leading underscore on top of its own, so it is two. */
__asm__(
    ".globl __chkstk\n"
    "__chkstk:\n"
    "    push %ecx\n"
    "    lea  8(%esp), %ecx\n"
    "    sub  %eax, %ecx\n"
    "    mov  %esp, %eax\n"
    "    mov  %ecx, %esp\n"
    "    mov  (%eax), %ecx\n"
    "    push 4(%eax)\n"
    "    ret\n");
