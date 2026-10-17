////////////////////////////////
//~ Sources

read_only global String8 ic_svgs[IC_Icon_COUNT] =
{
  {0},
  // folder.svg
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4.41707 12.4786H6.20854H11.5829C12.5724 12.4786 13.3744 11.6766 13.3744 10.6872V7.55212V7.10426C13.3744 6.11486 12.5724 5.3128 11.5829 5.3128H9.63778C9.16265 5.3128 8.70699 5.12406 8.37102 4.78808L7.62898 4.04604C7.29301 3.71007 6.83735 3.52133 6.36222 3.52133H4.41707C3.42768 3.52133 2.62561 4.3234 2.62561 5.3128V10.6872C2.62561 11.6766 3.42768 12.4786 4.41707 12.4786Z\" stroke=\"#DCE0E5\" stroke-width=\"1.2\"/>"
    "</svg>"),
  // rotate_cw.svg
  str8_lit_comp(
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" fill=\"none\">"
    "<path stroke=\"#000\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"1.2\" d=\"m13 5.778-1.256-1.256A5.416 5.416 0 0 0 8 3a5 5 0 1 0 4.583 7\"/>"
    "<path stroke=\"#000\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"1.2\" d=\"M13 3v3h-3\"/>"
    "</svg>"),
  // magnifying_glass.svg
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path opacity=\"0.1\" d=\"M3 7.31034C3 4.92981 4.92981 3 7.31034 3C9.6909 3 11.6207 4.92981 11.6207 7.31034C11.6207 9.6909 9.6909 11.6207 7.31034 11.6207C4.92981 11.6207 3 9.6909 3 7.31034Z\" fill=\"#DCE0E5\"/>"
    "<path d=\"M13 13L10.4138 10.4138M3 7.31034C3 4.92981 4.92981 3 7.31034 3C9.6909 3 11.6207 4.92981 11.6207 7.31034C11.6207 9.6909 9.6909 11.6207 7.31034 11.6207C4.92981 11.6207 3 9.6909 3 7.31034Z\" stroke=\"#DCE0E5\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>"),
  // moon
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path opacity=\"0.1\" d=\"M7.5 2.5A4.25 4.25 0 0 0 13.5 8.5A5.75 5.75 0 1 1 7.5 2.5Z\" fill=\"#DCE0E5\"/>"
    "<path d=\"M7.5 2.5A4.25 4.25 0 0 0 13.5 8.5A5.75 5.75 0 1 1 7.5 2.5Z\" stroke=\"#DCE0E5\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>"),
  // sun
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle opacity=\"0.1\" cx=\"8\" cy=\"8\" r=\"2.6\" fill=\"#DCE0E5\"/>"
    "<circle cx=\"8\" cy=\"8\" r=\"2.6\" stroke=\"#DCE0E5\" stroke-width=\"1.2\"/>"
    "<path d=\"M8 2V3.2M8 12.8V14M2 8H3.2M12.8 8H14M3.76 3.76L4.61 4.61M11.39 11.39L12.24 12.24M3.76 12.24L4.61 11.39M11.39 4.61L12.24 3.76\" stroke=\"#DCE0E5\" stroke-width=\"1.2\" stroke-linecap=\"round\"/>"
    "</svg>"),
  // list_tree.svg (the outline panel's icon)
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M13.5 8H9.5\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M13.5 4L6.5 4\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M13.5 12H9.5\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M3 3.5V6.33333C3 7.25 3.72 8 4.6 8H7\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M3 6V10.5C3 11.325 3.72 12 4.6 12H7\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>"),
  // close.svg
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4.70581 4.5L11.294 11.5M11.294 4.5L4.70581 11.5\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\"/>"
    "</svg>"),
  // file_icons/chevron_right.svg
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6.64062 4.64062L9.89062 8.00125L6.64062 11.375\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>"),
  // file_icons/chevron_down.svg
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4.63281 6.66406L7.99344 9.89844L11.3672 6.66406\" stroke=\"black\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>"),
  // file.svg
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 2V4.24394C9 4.5622 9.12643 4.86743 9.35147 5.09247C9.57652 5.31751 9.88174 5.44394 10.2 5.44394H12.5\" stroke=\"#DCE0E5\" stroke-width=\"1.2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M10.4283 2.79015C9.95564 2.28602 9.29544 2 8.60442 2H4.625C4.32663 2 4.04048 2.12643 3.8295 2.35147C3.61853 2.57652 3.5 2.88174 3.5 3.2V12.8C3.5 13.1183 3.61853 13.4235 3.8295 13.6485C4.04048 13.8736 4.32663 14 4.625 14H11.375C11.6734 14 11.9595 13.8736 12.1705 13.6485C12.3815 13.4235 12.5 13.1183 12.5 12.8V5.98862C12.5 5.35347 12.2582 4.74213 11.8238 4.27877L10.4283 2.79015Z\" stroke=\"#DCE0E5\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>"),
  // threads_sidebar_left_closed.svg (the sidebar toggle, closed)
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect opacity=\"0.1\" width=\"5\" height=\"12\" rx=\"2\" transform=\"matrix(-1 0 0 1 7 2)\" fill=\"#C6CAD0\"/>"
    "<path d=\"M7 2V14\" stroke=\"#C6CAD0\" stroke-width=\"1.2\"/>"
    "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\" rx=\"1.5\" stroke=\"#C6CAD0\" stroke-width=\"1.2\"/>"
    "</svg>"),
  // threads_sidebar_left_open.svg (the sidebar toggle, open)
  str8_lit_comp(
    "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect opacity=\"0.8\" width=\"5\" height=\"12\" rx=\"2\" transform=\"matrix(-1 0 0 1 7 2)\" fill=\"#C6CAD0\"/>"
    "<path d=\"M7 2V14\" stroke=\"#C6CAD0\" stroke-width=\"1.2\"/>"
    "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\" rx=\"1.5\" stroke=\"#C6CAD0\" stroke-width=\"1.2\"/>"
    "</svg>"),
};

////////////////////////////////
//~ Raster Cache

typedef struct IC_Raster IC_Raster;
struct IC_Raster
{
  IC_Icon icon;
  S32 px;
  R_Handle texture;              // zero when the SVG failed to rasterize
};

global IC_Raster ic_rasters[64];
global U64 ic_raster_count = 0;

internal void
ic_release_all(void)
{
  for EachIndex(i, ic_raster_count)
  {
    if(!r_handle_match(ic_rasters[i].texture, r_handle_zero())) { r_tex2d_release(ic_rasters[i].texture); }
  }
  ic_raster_count = 0;
}

internal R_Handle
ic_texture(IC_Icon icon, S32 px)
{
  for EachIndex(i, ic_raster_count)
  {
    if(ic_rasters[i].icon == icon && ic_rasters[i].px == px) { return ic_rasters[i].texture; }
  }
  // a full cache is only reached across many scale changes; start over
  if(ic_raster_count == ArrayCount(ic_rasters)) { ic_release_all(); }
  IC_Raster *raster = &ic_rasters[ic_raster_count++];
  raster->icon = icon;
  raster->px = px;
  raster->texture = r_handle_zero();
  Temp scratch = scratch_begin(0, 0);
  // nanosvg parses in place, so it gets a terminated copy
  char *src = (char *)push_str8_copy(scratch.arena, ic_svgs[icon]).str;
  NSVGimage *svg = nsvgParse(src, "px", 96.f);
  NSVGrasterizer *rast = nsvgCreateRasterizer();
  if(svg && rast && svg->width > 0)
  {
    U8 *pixels = push_array(scratch.arena, U8, (U64)px*px*4);
    nsvgRasterize(rast, svg, 0, 0, px/svg->width, pixels, px, px, px*4);
    for(S32 p = 0; p < px*px; p += 1) { pixels[p*4+0] = pixels[p*4+1] = pixels[p*4+2] = 255; }
    raster->texture = r_tex2d_alloc(R_ResourceKind_Static, v2s32(px, px), R_Tex2DFormat_RGBA8, pixels);
  }
  if(rast) { nsvgDeleteRasterizer(rast); }
  if(svg) { nsvgDelete(svg); }
  scratch_end(scratch);
  return raster->texture;
}

////////////////////////////////
//~ Drawing

// Draws `icon` at `px` physical pixels, centered in `r` on whole pixels.
internal void
ic_draw(IC_Icon icon, Rng2F32 r, F32 px, Vec4F32 color)
{
  S32 size = (S32)round_f32(px);
  if(icon <= IC_Icon_Null || icon >= IC_Icon_COUNT || size <= 0) { return; }
  R_Handle texture = ic_texture(icon, size);
  if(r_handle_match(texture, r_handle_zero())) { return; }
  F32 x0 = round_f32((r.x0 + r.x1 - size)/2);
  F32 y0 = round_f32((r.y0 + r.y1 - size)/2);
  dr_img(r2f32p(x0, y0, x0 + size, y0 + size), r2f32p(0, 0, (F32)size, (F32)size), texture, th_linear(color), 0, 0, 0);
}
