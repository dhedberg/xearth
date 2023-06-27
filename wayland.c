/*
 * wayland.c
 * David Hedberg
 * april 2021
 *
 * Heavily customized from x11.c
 *
 * The file has been kept somewhat ansi-ish for stylistic reasons, but
 * cannot actually be compiled with -ansi (without ugly define-hacks)
 */
/*
 * Copyright (C) 1989, 1990, 1993-1995, 1999 Kirk Lauritz Johnson
 *
 * Parts of the source code (as marked) are:
 *   Copyright (C) 1989, 1990, 1991 by Jim Frost
 *   Copyright (C) 1992 by Jamie Zawinski <jwz@lucid.com>
 *
 * Permission to use, copy, modify and freely distribute xearth for
 * non-commercial and not-for-profit purposes is hereby granted
 * without fee, provided that both the above copyright notice and this
 * permission notice appear in all copies and in supporting
 * documentation.
 *
 * Unisys Corporation holds worldwide patent rights on the Lempel Zev
 * Welch (LZW) compression technique employed in the CompuServe GIF
 * image file format as well as in other formats. Unisys has made it
 * clear, however, that it does not require licensing or fees to be
 * paid for freely distributed, non-commercial applications (such as
 * xearth) that employ LZW/GIF technology. Those wishing further
 * information about licensing the LZW patent should contact Unisys
 * directly at (lzw_info@unisys.com) or by writing to
 *
 *   Unisys Corporation
 *   Welch Licensing Department
 *   M/S-C1SW19
 *   P.O. Box 500
 *   Blue Bell, PA 19424
 *
 * The author makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT
 * OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#include <sys/mman.h>
#include <sys/param.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <cairo.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "xearth.h"

/* Using the _P macro here presumably makes no sense for modern code */

/* FIXME: We need to implement our own parsing to handle all
   parameters correctly */
extern void command_line _P((int, char *[]));

static void initialize_wayland _P((void));
static void setup_surface _P((void));
static void update_surface_geometry _P((uint32_t, uint32_t));
static void reconfigure_buffer _P((void));
static void render_frame _P((void));
static int  wayland_row _P((u_char *));
static void mark_location _P((MarkerInfo *));
static void draw_outlined_string _P((cairo_t *, int, int, char *));
static void draw_outlined_circle _P((cairo_t *, int, int));

static void global_registry_handler _P((void *, struct wl_registry *, uint32_t, const char *, uint32_t));
static void global_registry_remover _P((void *, struct wl_registry *, uint32_t));
static void xdg_surface_handle_configure _P((void *, struct xdg_surface *, uint32_t));
static void xdg_toplevel_handle_close _P((void *, struct xdg_toplevel *));
static void xdg_toplevel_configure _P((void *, struct xdg_toplevel *, int, int, struct wl_array *));
static void xdg_wm_base_ping _P((void *, struct xdg_wm_base *, uint32_t));
static void wlr_layer_surface_configure _P((void *, struct zwlr_layer_surface_v1 *, uint32_t, uint32_t, uint32_t));
static void wlr_layer_surface_closed _P((void *, struct zwlr_layer_surface_v1 *));

static struct wl_compositor              *compositor;
static struct wl_shm                     *shm;
static struct xdg_wm_base                *wm_base;
static struct zxdg_decoration_manager_v1 *xdg_decoration_manager;
static struct zwlr_layer_shell_v1        *wlr_layer_shell;
static struct zwlr_layer_surface_v1      *wlr_layer_surface;
static struct wl_display                 *display;
static struct xdg_toplevel               *xdg_toplevel;
static struct wl_surface                 *surface;

static struct wl_buffer                  *buffer;
static uint32_t                          *shm_data;
static size_t                             shm_data_size;

static int                                use_root;
static int                                need_redraw;
static int                                current_row;
static int                                running;

static const struct wl_registry_listener registry_listener = {
  global_registry_handler,
  global_registry_remover
};

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_handle_configure,
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  .configure = xdg_toplevel_configure,
  .close = xdg_toplevel_handle_close,
};

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_ping,
};

static const struct zwlr_layer_surface_v1_listener wlr_layer_surface_listener = {
  .configure = wlr_layer_surface_configure,
  .closed = wlr_layer_surface_closed,
};

static void global_registry_handler(data, registry, id, interface, version)
     void *data;
     struct wl_registry *registry;
     uint32_t id;
     const char *interface;
     uint32_t version;
{
  if (strcmp(interface, "wl_compositor") == 0)
  {
    if (version < 4)
      fatal("Need wl_compositor >= 4");

    compositor = wl_registry_bind(registry,
                                  id, &wl_compositor_interface, 4);
  }
  else if (strcmp(interface, wl_shm_interface.name) == 0)
    shm = wl_registry_bind(registry,
                           id, &wl_shm_interface, 1);
  else if (strcmp(interface, "xdg_wm_base") == 0)
    wm_base = wl_registry_bind(registry,
                                   id, &xdg_wm_base_interface, 1);
  else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
    xdg_decoration_manager =
      wl_registry_bind(registry, id,
                       &zxdg_decoration_manager_v1_interface, 1);
  else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
      wlr_layer_shell = wl_registry_bind(registry,
                                         id, &zwlr_layer_shell_v1_interface, 1);
}

static void global_registry_remover(data, registry, id)
     void *data;
     struct wl_registry *registry;
     uint32_t id;
{
  /* TODO: Something we need to handle? */
}


static void xdg_surface_handle_configure(data, xdg_surface, serial)
     void *data;
     struct xdg_surface *xdg_surface;
     uint32_t serial;
{
  xdg_surface_ack_configure(xdg_surface, serial);
  if (need_redraw)
  {
    render_frame();
    need_redraw = 0;
  }
}

static void xdg_toplevel_handle_close(data, xdg_toplevel)
     void *data;
     struct xdg_toplevel *xdg_toplevel;
{
  running = 0;
}

static void xdg_toplevel_configure(data, xdg_toplevel, width, height, states)
     void *data;
     struct xdg_toplevel *xdg_toplevel;
     int width;
     int height;
     struct wl_array *states;
{
  update_surface_geometry(width, height);
}

static void xdg_wm_base_ping(data, xdg_wm_base, serial)
     void *data;
     struct xdg_wm_base *xdg_wm_base;
     uint32_t serial;
{
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static void wlr_layer_surface_configure(data, surface, serial, width, height)
     void *data;
     struct zwlr_layer_surface_v1 *surface;
     uint32_t serial;
     uint32_t width;
     uint32_t height;
{
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  update_surface_geometry(width, height);
}

static void wlr_layer_surface_closed(data, surface)
     void *data;
     struct zwlr_layer_surface_v1 *surface;
{
  running = 0;
}

void command_line_w(argc, argv)
     int argc;
     char *argv[];
{
  int i;

  markerfile = "built-in";

  command_line(argc, argv);

  for (i=1; i<argc; i++)
  {
    if (strcmp(argv[i], "-root") == 0)
    {
      use_root = 1;
    }
  }

  initialize_wayland();

  setup_surface();

  reconfigure_buffer();
}

static void initialize_wayland()
{
  running = 1;
  need_redraw = 0;

  display = wl_display_connect(NULL);
  if (display == NULL)
    fatal("Could not connect to wayland");

  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);

  if (!wm_base || !compositor || !shm)
    fatal("One or more required interfaces missing");
}

static void setup_surface()
{
  struct xdg_surface *xdg_surface;

  surface = wl_compositor_create_surface(compositor);
  if (surface == NULL)
    fatal("Can't create surface");

  if (use_root)
  {
    if (!wlr_layer_shell)
      fatal("wlr_layer_shell is missing; cannot render to background\n");

    wlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(wlr_layer_shell,
                                                              surface,
                                                              NULL,
                                                              ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
                                                              "xearth");

    if (!wlr_layer_surface)
      fatal("wlr_layer_surface is null\n");

    zwlr_layer_surface_v1_set_size(wlr_layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(wlr_layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                                     | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                                     | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                                     | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_keyboard_interactivity(wlr_layer_surface,
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(wlr_layer_surface,
                                       &wlr_layer_surface_listener, wlr_layer_surface);
  }
  else
  {
    xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);

    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

    xdg_toplevel_set_title(xdg_toplevel, progname);

    if (xdg_decoration_manager)
    {
      struct zxdg_toplevel_decoration_v1 *decoration =
        zxdg_decoration_manager_v1_get_toplevel_decoration(xdg_decoration_manager,
                                                           xdg_toplevel);

      zxdg_toplevel_decoration_v1_set_mode(decoration,
                                           ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    else
      warning("xdg_toplevel_decoration not supported");
  }

  wl_surface_commit(surface);
  wl_display_roundtrip(display);
}

static void update_surface_geometry(width, height)
     uint32_t width;
     uint32_t height;
{
  if (width && height && (width != wdth || height != hght))
  {
    wdth = width;
    hght = height;

    reconfigure_buffer();

    need_redraw = 1;
  }
}

static void reconfigure_buffer()
{
  struct wl_shm_pool *pool;
  int stride = wdth*4;
  int size = 2*hght*stride;
  int fd;

  if (shm_data)
  {
    munmap(shm_data, shm_data_size);
    shm_data = NULL;
  }

  if (buffer)
  {
    wl_buffer_destroy(buffer);
    buffer = NULL;
  }

  fd = allocate_shm_file(size);
  shm_data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  shm_data_size = size;
  if (shm_data == MAP_FAILED)
    fatal("mmap failed");

  pool = wl_shm_create_pool(shm, fd, size);
  close(fd);

  buffer = wl_shm_pool_create_buffer(pool, 0, wdth, hght, stride,
                                     WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
}

void wayland_output()
{
  struct itimerspec timerspec;
  struct pollfd fds[2];
  int display_fd, timer_fd;
  int nfds;

  display_fd = wl_display_get_fd(display);
  if (display_fd < 0)
    fatal("Could not get display fd");

  if (wait_time > 0)
  {
    timerspec.it_interval.tv_sec = wait_time;
    timerspec.it_interval.tv_nsec = 0;
  }
  else
  {
    /* "Approximately zero" */
    timerspec.it_interval.tv_sec = 0;
    timerspec.it_interval.tv_nsec = 1;
  }
  timerspec.it_value = timerspec.it_interval;

  timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (timer_fd < 0)
    fatal("Could not get timer fd");

  timerfd_settime(timer_fd, 0, &timerspec, NULL);

  nfds = 2;

  fds[0].fd = display_fd;
  fds[0].events = POLLIN;

  fds[1].fd = timer_fd;
  fds[1].events = POLLIN;

  /* Keep it simple */
  render_frame();

  while (running)
  {
    uint64_t exp;
    int ret;

    while ( (ret = wl_display_flush(display)) > 0) {}
    if (ret < 0)
      fatal("wl_display_flush failed");

    ret = poll(fds, nfds, -1);
    if (ret == -1)
      fatal("Poll error");

    if (ret == 0)
      continue;

    if (fds[0].revents & POLLIN)
      wl_display_dispatch(display);

    if (fds[1].revents & POLLIN) {
      read(fds[1].fd, &exp, sizeof(uint64_t));
      render_frame();
    }
  }
}

static void render_frame()
{
  MarkerInfo *minfo;

  compute_positions();

  /* if we were really clever, we'd only
   * do this if the position has changed
   */
  scan_map();
  do_dots();

  /* for now, go ahead and reload the marker info every time
   * we redraw, but maybe change this in the future?
   */
  load_marker_info(markerfile);

  current_row = 0;
  render(wayland_row);

  if (do_markers)
  {
    minfo = marker_info;
    while (minfo->label != NULL)
    {
      mark_location(minfo);
      minfo += 1;
    }
  }

  wl_surface_set_buffer_scale(surface, 1);
  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surface);
}

static int wayland_row(row)
     u_char *row;
{
  int i;

  for (i = 0; i < wdth; i++)
  {
    unsigned char r = row[i*3];
    unsigned char g = row[i*3+1];
    unsigned char b = row[i*3+2];

    shm_data[current_row*wdth + i] = (0xff << 24) | (r << 16) | (g << 8) | b;
  }

  current_row++;
  return 0;
}

static void mark_location(info)
     MarkerInfo *info;
{
  cairo_surface_t *surface;
  cairo_t *cr;
  int         x, y;
  double      lat, lon;
  double      pos[3];
  char       *text;

  lat = info->lat * (M_PI/180);
  lon = info->lon * (M_PI/180);

  pos[0] = sin(lon) * cos(lat);
  pos[1] = sin(lat);
  pos[2] = cos(lon) * cos(lat);

  XFORM_ROTATE(pos, view_pos_info);

  if (proj_type == ProjTypeOrthographic)
  {
    /* if the marker isn't visible, return immediately
     */
    if (pos[2] <= 0) return;
  }
  else if (proj_type == ProjTypeMercator)
  {
    /* apply mercator projection
     */
    pos[0] = MERCATOR_X(pos[0], pos[2]);
    pos[1] = MERCATOR_Y(pos[1]);
  }
  else /* (proj_type == ProjTypeCylindrical) */
  {
    /* apply cylindrical projection
     */
    pos[0] = CYLINDRICAL_X(pos[0], pos[2]);
    pos[1] = CYLINDRICAL_Y(pos[1]);
  }

  x = XPROJECT(pos[0]);
  y = YPROJECT(pos[1]);

  surface = cairo_image_surface_create_for_data((unsigned char *)shm_data,
                                                CAIRO_FORMAT_ARGB32,
                                                wdth, hght, wdth*4);
  cr = cairo_create(surface);

  draw_outlined_circle(cr, x, y);

  text = info->label;
  if (text != NULL)
  {
    cairo_font_options_t *fo;
    cairo_font_extents_t fe;
    cairo_text_extents_t te;

    cairo_select_font_face(cr, "Helvetica",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);

    fo = cairo_font_options_create();
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
    cairo_set_font_options(cr, fo);

    cairo_font_extents(cr, &fe);
    cairo_text_extents(cr, text, &te);

    switch (info->align)
    {
    case MarkerAlignLeft:
      x -= (te.width + 4);
      y += (fe.ascent + fe.descent) / 3;
      break;

    case MarkerAlignRight:
    case MarkerAlignDefault:
      x += 3;
      y += (fe.ascent + fe.descent) / 3;
      break;

    case MarkerAlignAbove:
      x -= (te.width) / 2;
      y -= (fe.descent + 4);
      break;

    case MarkerAlignBelow:
      x -= (te.width) / 2;
      y += (fe.ascent + 5);
      break;

    default:
      assert(0);
    }

    draw_outlined_string(cr, x, y, text);
    cairo_font_options_destroy(fo);
  }

  cairo_surface_finish(surface);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
}

static void draw_outlined_circle(cr, x, y)
     cairo_t *cr;
     int x;
     int y;
{
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_line_width(cr, 1.0);

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_arc(cr, x, y, 4, 0, 2*M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, x, y, 2, 0, 2*M_PI);
  cairo_stroke(cr);

  cairo_set_source_rgb (cr, 1.0, 0.0, 0.0);
  cairo_arc(cr, x, y, 3, 0, 2*M_PI);
  cairo_stroke(cr);
}

static void draw_outlined_string(cr, x, y, text)
     cairo_t *cr;
     int x;
     int y;
     char *text;
{

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_move_to(cr, x+1, y);
  cairo_show_text (cr, text);
  cairo_move_to(cr, x-1, y);
  cairo_show_text (cr, text);
  cairo_move_to(cr, x, y+1);
  cairo_show_text (cr, text);
  cairo_move_to(cr, x, y-1);
  cairo_show_text (cr, text);

  cairo_set_source_rgb (cr, 1.0, 0.0, 0.0);
  cairo_move_to(cr, x, y);
  cairo_show_text (cr, text);
}
