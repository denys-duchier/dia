/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* Copyright 2003, W. Borgert <debacle@debian.org>
   copied a lot from UML/large_package.c and standard/polygon.c */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <math.h>
#include <string.h>

#include "intl.h"
#include "object.h"
#include "polyshape.h"
#include "diarenderer.h"
#include "attributes.h"
#include "text.h"
#include "properties.h"

#include "network.h"

#include "pixmaps/radiocell.xpm"

typedef struct _RadioCell RadioCell;

struct _RadioCell {
  PolyShape poly;		/* always 1st! */
  real radius;			/* pseudo-radius */
  Point center;			/* point in the center */
  Color line_colour;
  LineStyle line_style;
  real dashlength;
  real line_width;
  gboolean show_background;
  Color fill_colour;
  Text *text;
  TextAttributes attrs;
};

#define RADIOCELL_LINEWIDTH  0.1
#define RADIOCELL_FONTHEIGHT 0.8

static real radiocell_distance_from(RadioCell *radiocell, Point *point);
static void radiocell_select(RadioCell *radiocell, Point *clicked_point,
			     DiaRenderer *interactive_renderer);
static ObjectChange* radiocell_move_handle(RadioCell *radiocell,
					   Handle *handle,
					   Point *to, ConnectionPoint *cp,
					   HandleMoveReason reason,
					   ModifierKeys modifiers);
static ObjectChange* radiocell_move(RadioCell *radiocell, Point *to);
static void radiocell_draw(RadioCell *radiocell, DiaRenderer *renderer);
static DiaObject *radiocell_create(Point *startpoint,
				void *user_data,
				Handle **handle1,
				Handle **handle2);
static void radiocell_destroy(RadioCell *radiocell);
static void radiocell_update_data(RadioCell *radiocell);
static PropDescription *radiocell_describe_props(RadioCell *radiocell);
static void radiocell_get_props(RadioCell *radiocell, GPtrArray *props);
static void radiocell_set_props(RadioCell *radiocell, GPtrArray *props);
static DiaObject *radiocell_load(ObjectNode obj_node, int version,DiaContext *ctx);

static ObjectTypeOps radiocell_type_ops =
{
  (CreateFunc)        radiocell_create,
  (LoadFunc)          radiocell_load,
  (SaveFunc)          object_save_using_properties,
  (GetDefaultsFunc)   NULL,
  (ApplyDefaultsFunc) NULL
};

DiaObjectType radiocell_type =
{
  "Network - Radio Cell",	/* name */
  0,				/* version */
  (char **) radiocell_xpm,	/* pixmap */

  &radiocell_type_ops		/* ops */
};

static ObjectOps radiocell_ops = {
  (DestroyFunc)         radiocell_destroy,
  (DrawFunc)            radiocell_draw,
  (DistanceFunc)        radiocell_distance_from,
  (SelectFunc)          radiocell_select,
  (CopyFunc)            object_copy_using_properties,
  (MoveFunc)            radiocell_move,
  (MoveHandleFunc)      radiocell_move_handle,
  (GetPropertiesFunc)   object_create_props_dialog,
  (ApplyPropertiesDialogFunc) object_apply_props_from_dialog,
  (ObjectMenuFunc)      NULL,
  (DescribePropsFunc)   radiocell_describe_props,
  (GetPropsFunc)        radiocell_get_props,
  (SetPropsFunc)        radiocell_set_props,
  (TextEditFunc) 0,
  (ApplyPropertiesListFunc) object_apply_props,
};

static PropDescription radiocell_props[] = {
  POLYSHAPE_COMMON_PROPERTIES,
  { "radius", PROP_TYPE_REAL, 0, N_("Radius"), NULL, NULL },
  PROP_STD_LINE_WIDTH,
  PROP_STD_LINE_COLOUR,
  PROP_STD_LINE_STYLE,
  PROP_STD_FILL_COLOUR,
  PROP_STD_SHOW_BACKGROUND,
  { "text", PROP_TYPE_TEXT, 0, N_("Text"), NULL, NULL },
  PROP_STD_TEXT_FONT,
  PROP_STD_TEXT_HEIGHT,
  PROP_STD_TEXT_COLOUR,
  PROP_STD_TEXT_ALIGNMENT,
  PROP_DESC_END
};

static PropDescription *
radiocell_describe_props(RadioCell *radiocell)
{
  if (radiocell_props[0].quark == 0) {
    prop_desc_list_calculate_quarks(radiocell_props);
  }
  return radiocell_props;
}

static PropOffset radiocell_offsets[] = {
  POLYSHAPE_COMMON_PROPERTIES_OFFSETS,
  { "radius", PROP_TYPE_REAL, offsetof(RadioCell, radius) },
  { PROP_STDNAME_LINE_WIDTH, PROP_STDTYPE_LINE_WIDTH, offsetof(RadioCell, line_width) },
  { "line_colour", PROP_TYPE_COLOUR, offsetof(RadioCell, line_colour) },
  { "line_style", PROP_TYPE_LINESTYLE,
    offsetof(RadioCell, line_style), offsetof(RadioCell, dashlength) },
  { "fill_colour", PROP_TYPE_COLOUR, offsetof(RadioCell, fill_colour) },
  { "show_background", PROP_TYPE_BOOL,
    offsetof(RadioCell, show_background) },
  { "text", PROP_TYPE_TEXT, offsetof(RadioCell, text) },
  { "text_font", PROP_TYPE_FONT, offsetof(RadioCell, attrs.font) },
  { PROP_STDNAME_TEXT_HEIGHT, PROP_STDTYPE_TEXT_HEIGHT, offsetof(RadioCell, attrs.height) },
  { "text_colour", PROP_TYPE_COLOUR, offsetof(RadioCell, attrs.color) },
  { "text_alignment", PROP_TYPE_ENUM,
    offsetof(RadioCell, attrs.alignment) },
  { NULL, 0, 0 },
};

static void
radiocell_get_props(RadioCell *radiocell, GPtrArray *props)
{
  text_get_attributes(radiocell->text, &radiocell->attrs);
  object_get_props_from_offsets(&radiocell->poly.object,
                                radiocell_offsets, props);
}

static void
radiocell_set_props(RadioCell *radiocell, GPtrArray *props)
{
  object_set_props_from_offsets(&radiocell->poly.object,
                                radiocell_offsets, props);
  apply_textattr_properties(props, radiocell->text,
			    "text", &radiocell->attrs);
  radiocell_update_data(radiocell);
}

static real
radiocell_distance_from(RadioCell *radiocell, Point *point)
{
  return polyshape_distance_from(&radiocell->poly, point,
				 radiocell->line_width);
}

static void
radiocell_select(RadioCell *radiocell, Point *clicked_point,
		 DiaRenderer *interactive_renderer)
{
  text_set_cursor(radiocell->text, clicked_point, interactive_renderer);
  text_grab_focus(radiocell->text, &radiocell->poly.object);
  radiocell_update_data(radiocell);
}

static ObjectChange*
radiocell_move_handle(RadioCell *radiocell, Handle *handle,
		      Point *to, ConnectionPoint *cp,
		      HandleMoveReason reason, ModifierKeys modifiers)
{
  real distance;
  gboolean larger;

  /* prevent flicker for "negative" resizing */
  if ((handle->id == HANDLE_CUSTOM1 && to->x < radiocell->center.x) ||
      (handle->id == HANDLE_CUSTOM4 && to->x > radiocell->center.x) ||
      ((handle->id == HANDLE_CUSTOM2 || handle->id == HANDLE_CUSTOM3) &&
       to->y < radiocell->center.y) ||
      ((handle->id == HANDLE_CUSTOM5 || handle->id == HANDLE_CUSTOM6) &&
       to->y > radiocell->center.y)) {
    return NULL;
  }

  /* prevent flicker for "diagonal" resizing */
  if (handle->id == HANDLE_CUSTOM1 || handle->id == HANDLE_CUSTOM4) {
    to->y = handle->pos.y;
  }
  else {
    to->x = handle->pos.x;
  }

  distance = distance_point_point(&handle->pos, to);
  larger = distance_point_point(&handle->pos, &radiocell->center) <
    distance_point_point(to, &radiocell->center);
  radiocell->radius += distance * (larger? 1: -1);
  if (radiocell->radius < 1.)
    radiocell->radius = 1.;
  radiocell_update_data(radiocell);

  return NULL;
}

static ObjectChange*
radiocell_move(RadioCell *radiocell, Point *to)
{
  polyshape_move(&radiocell->poly, to);
  radiocell->center = *to;
  radiocell->center.x -= radiocell->radius;
  radiocell_update_data(radiocell);

  return NULL;
}

static void
radiocell_draw(RadioCell *radiocell, DiaRenderer *renderer)
{
  DiaRendererClass *renderer_ops = DIA_RENDERER_GET_CLASS (renderer);
  PolyShape *poly;
  Point *points;
  int n;

  assert(radiocell != NULL);
  assert(renderer != NULL);

  poly = &radiocell->poly;
  points = &poly->points[0];
  n = poly->numpoints;

  if (radiocell->show_background) {
    renderer_ops->set_fillstyle(renderer, FILLSTYLE_SOLID);
    renderer_ops->fill_polygon(renderer, points, n, &radiocell->fill_colour);
  }
  renderer_ops->set_linecaps(renderer, LINECAPS_BUTT);
  renderer_ops->set_linejoin(renderer, LINEJOIN_MITER);
  renderer_ops->set_linestyle(renderer, radiocell->line_style);
  renderer_ops->set_linewidth(renderer, radiocell->line_width);
  renderer_ops->set_dashlength(renderer, radiocell->dashlength);
  renderer_ops->draw_polygon(renderer, points, n, &radiocell->line_colour);

  text_draw(radiocell->text, renderer);
}

static void
radiocell_update_data(RadioCell *radiocell)
{
  PolyShape *poly = &radiocell->poly;
  DiaObject *obj = &poly->object;
  ElementBBExtras *extra = &poly->extra_spacing;
  Rectangle text_box;
  Point textpos;
  int i;
  /* not exactly a regular hexagon, but this fits better in the grid */
  Point points[] = { {  1., 0. }, {  .5,  .75 }, { -.5,  .75 },
		     { -1., 0. }, { -.5, -.75 }, {  .5, -.75 } };

  radiocell->center.x = (poly->points[0].x + poly->points[3].x) / 2.;
  radiocell->center.y = poly->points[0].y;

  for (i = 0; i < 6; i++) {
    poly->points[i] = radiocell->center;
    poly->points[i].x += radiocell->radius * points[i].x;
    poly->points[i].y += radiocell->radius * points[i].y;
  }

  /* Add bounding box for text: */
  text_calc_boundingbox(radiocell->text, NULL);
  textpos.x = (poly->points[0].x + poly->points[3].x) / 2.;
  textpos.y = poly->points[0].y -
    (radiocell->text->height * (radiocell->text->numlines - 1) +
     radiocell->text->descent) / 2.;
  text_set_position(radiocell->text, &textpos);
  text_calc_boundingbox(radiocell->text, &text_box);
  polyshape_update_data(poly);
  extra->border_trans = radiocell->line_width / 2.0;
  polyshape_update_boundingbox(poly);
  rectangle_union(&obj->bounding_box, &text_box);
  obj->position = poly->points[0];
}

static DiaObject *
radiocell_create(Point *startpoint,
		 void *user_data,
		 Handle **handle1,
		 Handle **handle2)
{
  RadioCell *radiocell;
  PolyShape *poly;
  DiaObject *obj;
  DiaFont *font;
  int i = 0;

  radiocell = g_new0(RadioCell, 1);
  poly = &radiocell->poly;
  obj = &poly->object;
  obj->type = &radiocell_type;
  obj->ops = &radiocell_ops;
  obj->flags |= DIA_OBJECT_CAN_PARENT;

  radiocell->radius = 4.;

  /* do not use default_properties.show_background here */
  radiocell->show_background = FALSE;
  radiocell->fill_colour = color_white;
  radiocell->line_colour = color_black;
  radiocell->line_width = RADIOCELL_LINEWIDTH;
  attributes_get_default_line_style(&radiocell->line_style,
				    &radiocell->dashlength);

  font = dia_font_new_from_style(DIA_FONT_MONOSPACE, RADIOCELL_FONTHEIGHT);
  radiocell->text = new_text("", font, RADIOCELL_FONTHEIGHT, startpoint,
			     &color_black, ALIGN_CENTER);
  dia_font_unref(font);
  text_get_attributes(radiocell->text, &radiocell->attrs);

  polyshape_init(poly, 6);

  radiocell->center = *startpoint;
  /* rather broken but otherwise we will always calculate from unitialized data
   * see polyshape_update_data() */
  poly->points[0].y = startpoint->y;
  poly->points[0].x = startpoint->x - radiocell->radius;
  poly->points[3].x = startpoint->x + radiocell->radius;

  radiocell_update_data(radiocell);
  *handle1 = poly->object.handles[0];
  *handle2 = poly->object.handles[2];

  for (i=0;i<6;i++) {
    poly->object.handles[i]->id = HANDLE_CUSTOM1 + i;
  }

  return &radiocell->poly.object;
}

static void
radiocell_destroy(RadioCell *radiocell)
{
  text_destroy(radiocell->text);
  polyshape_destroy(&radiocell->poly);
}

static DiaObject *
radiocell_load(ObjectNode obj_node, int version,DiaContext *ctx)
{
  return object_load_using_properties(&radiocell_type,
                                      obj_node, version,ctx);
}
