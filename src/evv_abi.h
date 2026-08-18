/* How our names meet the original's.
 *
 * The differential build links our code beside IBM's own objects, so a
 * function of ours that stands in for one of theirs has to answer to the
 * name their objects call it by: an MSVC-mangled name, in an object format
 * where every C symbol also carries a leading underscore and every stdcall
 * one a byte count. ALIAS puts that name on our function; MANGLED asks for
 * one of theirs by it.
 *
 * A native build has no IBM objects in it, so none of that applies, and it
 * could not be made to apply anyway: ELF holds those names happily enough
 * in a symbol table, but the assembler will not accept a question mark or
 * an at sign in a call operand, so a mangled name can be defined and never
 * called. Natively, then, every function simply keeps its own name, ALIAS
 * emits nothing, and MANGLED asks for the name as written -- which is what
 * makes a missing one show up in the link as something readable.
 *
 * The two calling conventions stay as they are on both targets. They are
 * i386 conventions, not Windows ones, and gcc honours them either side; a
 * vtable laid out one way and called the other would be the sort of fault
 * that only shows up as noise coming out of the synthesiser.
 */

#ifndef EVV_ABI_H
#define EVV_ABI_H

#define THIS    __attribute__((thiscall))
#define STDCALL __attribute__((stdcall))

#if defined(_WIN32)

#define MANGLED(name) __asm__("\"" name "\"")

#define ALIAS(mangled, ours) \
    __asm__(".globl \"" mangled "\"\n.set \"" mangled "\", _" ours "\n")

/* A stdcall name carries the size of its arguments, so these need the
   alias that puts it back. */
#define ALIAS_N(mangled, ours, n) \
    __asm__(".globl \"" mangled "\"\n.set \"" mangled "\", _" ours "@" #n "\n")

/* The compiler put a copy of some of these in every object that used one,
   so ours has to give way to whichever copy is still linked. */
#define WEAK_ALIAS(mangled, ours) \
    __asm__(".weak \"" mangled "\"\n.set \"" mangled "\", _" ours "\n")

#else

#define MANGLED(name)
#define ALIAS(mangled, ours)
#define ALIAS_N(mangled, ours, n)
#define WEAK_ALIAS(mangled, ours)

#endif

#endif
