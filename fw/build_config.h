/* Tau firmware build capabilities.
 *
 * The standard build must remain predictable and user-facing. Experimental
 * controls and runtime diagnostics may only be compiled when this capability
 * is enabled, and must still remain hidden until a future in-app Advanced
 * setting is explicitly enabled by the user.
 *
 * Desktop framebuffer snapshots are build tooling, not a runtime feature, and
 * are deliberately independent of this flag.
 */
#ifndef TAU_BUILD_CONFIG_H
#define TAU_BUILD_CONFIG_H

#ifndef TAU_ADVANCED_BUILD
#define TAU_ADVANCED_BUILD 0
#endif

#if TAU_ADVANCED_BUILD != 0 && TAU_ADVANCED_BUILD != 1
#error "TAU_ADVANCED_BUILD must be 0 or 1"
#endif

#endif
