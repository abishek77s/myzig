// icons: Zed's icons (assets/icons, crates/ui/src/components/icon.rs), plus a
// moon and a sun drawn the same way: a 16px grid, 1.2px round strokes, the
// body under a 0.1 fill.
//
// Icons are monochrome and tinted when drawn (`Icon::color`), so each one is
// rasterized with nanosvg to white coverage at the size it is drawn at, and
// tinted by dr_img's color.

#ifndef ICONS_H
#define ICONS_H

typedef enum IC_Icon
{
  IC_Icon_Null,
  IC_Icon_Folder,
  IC_Icon_RotateCw,
  IC_Icon_MagnifyingGlass,
  IC_Icon_Moon,
  IC_Icon_Sun,
  IC_Icon_ListTree,
  IC_Icon_Close,
  IC_Icon_ChevronRight,
  IC_Icon_ChevronDown,
  IC_Icon_File,
  IC_Icon_SidebarLeftClosed,
  IC_Icon_SidebarLeftOpen,
  IC_Icon_COUNT
}
IC_Icon;

// IconSize
#define IC_SIZE_XSMALL_PX 12.f
#define IC_SIZE_SMALL_PX  14.f
#define IC_SIZE_MEDIUM_PX 16.f

internal void ic_draw(IC_Icon icon, Rng2F32 r, F32 px, Vec4F32 color);
internal void ic_release_all(void);

#endif // ICONS_H
