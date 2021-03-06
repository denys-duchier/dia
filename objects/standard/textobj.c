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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <math.h>

#include "intl.h"
#include "object.h"
#include "connectionpoint.h"
#include "diarenderer.h"
#include "font.h"
#include "text.h"
#include "attributes.h"
#include "properties.h"
#include "diamenu.h"
#include "create.h"

#include "tool-icons.h"

#define HANDLE_TEXT HANDLE_CUSTOM1


typedef enum _Valign Valign;
enum _Valign {
        VALIGN_TOP,
        VALIGN_BOTTOM,
        VALIGN_CENTER,
        VALIGN_FIRST_LINE
};
typedef struct _Textobj Textobj;
/*!
 * \brief Standard - Text
 * \extends _DiaObject
 * \ingroup StandardObjects
 */
struct _Textobj {
  DiaObject object;
  /*! just one handle to connect/move */
  Handle text_handle;
  /*! the real text object to be drawn */
  Text *text;
  /*! synched copy of attributes from _Text object */
  TextAttributes attrs;
  /*! vertical alignment of the whole text block */
  Valign vert_align;
  /*! bounding box filling */
  Color fill_color;
  /*! backround to be filled or transparent */
  gboolean show_background;
};

static struct _TextobjProperties {
  Alignment alignment;
  Valign vert_align;
} default_properties = { ALIGN_LEFT, VALIGN_FIRST_LINE } ;

static real textobj_distance_from(Textobj *textobj, Point *point);
static void textobj_select(Textobj *textobj, Point *clicked_point,
			   DiaRenderer *interactive_renderer);
static ObjectChange* textobj_move_handle(Textobj *textobj, Handle *handle,
					 Point *to, ConnectionPoint *cp,
					 HandleMoveReason reason, ModifierKeys modifiers);
static ObjectChange* textobj_move(Textobj *textobj, Point *to);
static void textobj_draw(Textobj *textobj, DiaRenderer *renderer);
static void textobj_update_data(Textobj *textobj);
static DiaObject *textobj_create(Point *startpoint,
			      void *user_data,
			      Handle **handle1,
			      Handle **handle2);
static void textobj_destroy(Textobj *textobj);

static void textobj_get_props(Textobj *textobj, GPtrArray *props);
static void textobj_set_props(Textobj *textobj, GPtrArray *props);

static void textobj_save(Textobj *textobj, ObjectNode obj_node,
			 const char *filename);
static DiaObject *textobj_load(ObjectNode obj_node, int version, DiaContext *ctx);
static DiaMenu *textobj_get_object_menu(Textobj *textobj, Point *clickedpoint);

static void textobj_valign_point(Textobj *textobj, Point* p, real factor);

static ObjectTypeOps textobj_type_ops =
{
  (CreateFunc) textobj_create,
  (LoadFunc)   textobj_load,
  (SaveFunc)   textobj_save,
  (GetDefaultsFunc)   NULL, 
  (ApplyDefaultsFunc) NULL
};

PropEnumData prop_text_vert_align_data[] = {
  { N_("Bottom"), VALIGN_BOTTOM },
  { N_("Top"), VALIGN_TOP },
  { N_("Center"), VALIGN_CENTER },
  { N_("First Line"), VALIGN_FIRST_LINE },
  { NULL, 0 }
};
static PropDescription textobj_props[] = {
  OBJECT_COMMON_PROPERTIES,
  PROP_STD_TEXT_ALIGNMENT,
  { "text_vert_alignment", PROP_TYPE_ENUM, PROP_FLAG_VISIBLE | PROP_FLAG_OPTIONAL, \
    N_("Vertical text alignment"), NULL, prop_text_vert_align_data },
  PROP_STD_TEXT_FONT,
  PROP_STD_TEXT_HEIGHT,
  PROP_STD_TEXT_COLOUR,
  PROP_STD_SAVED_TEXT,
  PROP_STD_FILL_COLOUR_OPTIONAL,
  PROP_STD_SHOW_BACKGROUND_OPTIONAL,
  PROP_DESC_END
};

static PropOffset textobj_offsets[] = {
  OBJECT_COMMON_PROPERTIES_OFFSETS,
  {"text",PROP_TYPE_TEXT,offsetof(Textobj,text)},
  {"text_font",PROP_TYPE_FONT,offsetof(Textobj,attrs.font)},
  {PROP_STDNAME_TEXT_HEIGHT,PROP_STDTYPE_TEXT_HEIGHT,offsetof(Textobj,attrs.height)},
  {"text_colour",PROP_TYPE_COLOUR,offsetof(Textobj,attrs.color)},
  {"text_alignment",PROP_TYPE_ENUM,offsetof(Textobj,attrs.alignment)},
  {"text_vert_alignment",PROP_TYPE_ENUM,offsetof(Textobj,vert_align)},
  { "fill_colour", PROP_TYPE_COLOUR, offsetof(Textobj, fill_color) },
  { "show_background", PROP_TYPE_BOOL, offsetof(Textobj, show_background) },
  { NULL, 0, 0 }
};

/* Version history:
 * Version 1 added vertical alignment, and needed old objects to use the
 *     right alignment.
 */
DiaObjectType textobj_type =
{
  "Standard - Text",   /* name */
  1,                   /* version */
  (char **) text_icon, /* pixmap */
  &textobj_type_ops,   /* ops */
  NULL,
  0,
  textobj_props,
  textobj_offsets
};

DiaObjectType *_textobj_type = (DiaObjectType *) &textobj_type;

static ObjectOps textobj_ops = {
  (DestroyFunc)         textobj_destroy,
  (DrawFunc)            textobj_draw,
  (DistanceFunc)        textobj_distance_from,
  (SelectFunc)          textobj_select,
  (CopyFunc)            object_copy_using_properties,
  (MoveFunc)            textobj_move,
  (MoveHandleFunc)      textobj_move_handle,
  (GetPropertiesFunc)   object_create_props_dialog,
  (ApplyPropertiesDialogFunc) object_apply_props_from_dialog,
  (ObjectMenuFunc)      textobj_get_object_menu,
  (DescribePropsFunc)   object_describe_props,
  (GetPropsFunc)        textobj_get_props,
  (SetPropsFunc)        textobj_set_props,
  (TextEditFunc) 0,
  (ApplyPropertiesListFunc) object_apply_props,
};

static void
textobj_get_props(Textobj *textobj, GPtrArray *props)
{
  text_get_attributes(textobj->text,&textobj->attrs);
  object_get_props_from_offsets(&textobj->object,textobj_offsets,props);
}

static void
textobj_set_props(Textobj *textobj, GPtrArray *props)
{
  object_set_props_from_offsets(&textobj->object,textobj_offsets,props);
  apply_textattr_properties(props,textobj->text,"text",&textobj->attrs);
  textobj_update_data(textobj);
}

static real
textobj_distance_from(Textobj *textobj, Point *point)
{
  return text_distance_from(textobj->text, point); 
}

static void
textobj_select(Textobj *textobj, Point *clicked_point,
	       DiaRenderer *interactive_renderer)
{
  text_set_cursor(textobj->text, clicked_point, interactive_renderer);
  text_grab_focus(textobj->text, &textobj->object);
}



static ObjectChange*
textobj_move_handle(Textobj *textobj, Handle *handle,
		    Point *to, ConnectionPoint *cp,
		    HandleMoveReason reason, ModifierKeys modifiers)
{
  assert(textobj!=NULL);
  assert(handle!=NULL);
  assert(to!=NULL);

  if (handle->id == HANDLE_TEXT) {
          /*Point to2 = *to;
          point_add(&to2,&textobj->text->position);
          point_sub(&to2,&textobj->text_handle.pos);
          textobj_move(textobj, &to2);*/
          textobj_move(textobj, to);
          
  }

  return NULL;
}

static ObjectChange*
textobj_move(Textobj *textobj, Point *to)
{
  textobj->object.position = *to;

  textobj_update_data(textobj);

  return NULL;
}

static void
textobj_draw(Textobj *textobj, DiaRenderer *renderer)
{
  assert(textobj != NULL);
  assert(renderer != NULL);

  if (textobj->show_background) {
    Rectangle box;
    Point ul, lr;
    text_calc_boundingbox (textobj->text, &box);
    ul.x = box.left;
    ul.y = box.top;
    lr.x = box.right;
    lr.y = box.bottom;
    DIA_RENDERER_GET_CLASS (renderer)->fill_rect (renderer, &ul, &lr, &textobj->fill_color);
  }
  text_draw(textobj->text, renderer);
}

static void textobj_valign_point(Textobj *textobj, Point* p, real factor)
        /* factor should be 1 or -1 */
{
    Rectangle *bb  = &(textobj->object.bounding_box); 
    real offset ;
    switch (textobj->vert_align){
        case VALIGN_BOTTOM:
            offset = bb->bottom - textobj->object.position.y;
            p->y -= offset * factor; 
            break;
        case VALIGN_TOP:
            offset = bb->top - textobj->object.position.y;
            p->y -= offset * factor; 
            break;
        case VALIGN_CENTER:
            offset = (bb->bottom + bb->top)/2 - textobj->object.position.y;
            p->y -= offset * factor; 
            break;
        case VALIGN_FIRST_LINE:
            break;
        }
}
static void
textobj_update_data(Textobj *textobj)
{
  Point to2;
  DiaObject *obj = &textobj->object;
  
  text_set_position(textobj->text, &obj->position);
  text_calc_boundingbox(textobj->text, &obj->bounding_box);

  to2 = obj->position;
  textobj_valign_point(textobj, &to2, 1);
  text_set_position(textobj->text, &to2);
  text_calc_boundingbox(textobj->text, &obj->bounding_box);
  
  textobj->text_handle.pos = obj->position;
}

static DiaObject *
textobj_create(Point *startpoint,
	       void *user_data,
	       Handle **handle1,
	       Handle **handle2)
{
  Textobj *textobj;
  DiaObject *obj;
  Color col;
  DiaFont *font = NULL;
  real font_height;
  
  textobj = g_malloc0(sizeof(Textobj));
  obj = &textobj->object;
  
  obj->type = &textobj_type;

  obj->ops = &textobj_ops;

  col = attributes_get_foreground();
  attributes_get_default_font(&font, &font_height);
  textobj->text = new_text("", font, font_height,
			   startpoint, &col, default_properties.alignment );
  /* need to initialize to object.position as well, it is used update data */
  obj->position = *startpoint;

  text_get_attributes(textobj->text,&textobj->attrs);
  dia_font_unref(font);
  textobj->vert_align = default_properties.vert_align;
  
  /* default visibility must be off to keep compatibility */
  textobj->fill_color = attributes_get_background();
  textobj->show_background = FALSE;
  
  object_init(obj, 1, 0);

  obj->handles[0] = &textobj->text_handle;
  textobj->text_handle.id = HANDLE_TEXT;
  textobj->text_handle.type = HANDLE_MAJOR_CONTROL;
  textobj->text_handle.connect_type = HANDLE_CONNECTABLE;
  textobj->text_handle.connected_to = NULL;

  textobj_update_data(textobj);

  *handle1 = NULL;
  *handle2 = obj->handles[0];
  return &textobj->object;
}

static void
textobj_destroy(Textobj *textobj)
{
  text_destroy(textobj->text);
  dia_font_unref(textobj->attrs.font);
  object_destroy(&textobj->object);
}

static void
textobj_save(Textobj *textobj, ObjectNode obj_node, const char *filename)
{
  object_save(&textobj->object, obj_node);

  data_add_text(new_attribute(obj_node, "text"),
		textobj->text);
  data_add_enum(new_attribute(obj_node, "valign"),
		  textobj->vert_align);

  if (textobj->show_background) {
    data_add_color(new_attribute(obj_node, "fill_color"), &textobj->fill_color);
    data_add_boolean(new_attribute(obj_node, "show_background"), textobj->show_background);
  }
}

static DiaObject *
textobj_load(ObjectNode obj_node, int version, DiaContext *ctx)
{
  Textobj *textobj;
  DiaObject *obj;
  AttributeNode attr;
  Point startpoint = {0.0, 0.0};

  textobj = g_malloc0(sizeof(Textobj));
  obj = &textobj->object;
  
  obj->type = &textobj_type;
  obj->ops = &textobj_ops;

  object_load(obj, obj_node, ctx);

  attr = object_find_attribute(obj_node, "text");
  if (attr != NULL) {
    textobj->text = data_text(attribute_first_data(attr), ctx);
  } else {
    DiaFont* font = dia_font_new_from_style(DIA_FONT_MONOSPACE,1.0);
    textobj->text = new_text("", font, 1.0,
			     &startpoint, &color_black, ALIGN_CENTER);
    dia_font_unref(font);
  }
  /* initialize attrs from text */
  text_get_attributes(textobj->text,&textobj->attrs);

  attr = object_find_attribute(obj_node, "valign");
  if (attr != NULL)
    textobj->vert_align = data_enum(attribute_first_data(attr), ctx);
  else if (version == 0) {
    textobj->vert_align = VALIGN_FIRST_LINE;
  }

  /* default visibility must be off to keep compatibility */
  textobj->fill_color = attributes_get_background();
  attr = object_find_attribute(obj_node, "fill_color");
  if (attr)
    data_color(attribute_first_data(attr), &textobj->fill_color, ctx);
  attr = object_find_attribute(obj_node, "show_background");
  if (attr)
    textobj->show_background = data_boolean(attribute_first_data(attr), ctx);
  else
    textobj->show_background = FALSE;

  object_init(obj, 1, 0);

  obj->handles[0] = &textobj->text_handle;
  textobj->text_handle.id = HANDLE_TEXT;
  textobj->text_handle.type = HANDLE_MAJOR_CONTROL;
  textobj->text_handle.connect_type = HANDLE_CONNECTABLE;
  textobj->text_handle.connected_to = NULL;

  textobj_update_data(textobj);

  return &textobj->object;
}

static ObjectChange *
_textobj_convert_to_path_callback (DiaObject *obj, Point *clicked, gpointer data)
{
  Textobj *textobj = (Textobj *)obj;
  const Text *text = textobj->text;
  DiaObject *path = NULL;

  if (!text_is_empty(text)) /* still screwed with empty lines ;) */
    path = create_standard_path_from_text (text);

  if (path) {
    ObjectChange *change;
    Color bg = textobj->fill_color;

    /* FIXME: otherwise object_substitue() will tint the text with bg */
    textobj->fill_color = text->color;
    change = object_substitute (obj, path);
    /* restore */
    textobj->fill_color = bg;

    return change;
  }
  /* silently fail */
  return change_list_create ();
}
static DiaMenuItem textobj_menu_items[] = {
  { N_("Convert to Path"), _textobj_convert_to_path_callback, NULL, DIAMENU_ACTIVE }
};

static DiaMenu textobj_menu = {
  "Text",
  sizeof(textobj_menu_items)/sizeof(DiaMenuItem),
  textobj_menu_items,
  NULL
};

static DiaMenu *
textobj_get_object_menu(Textobj *textobj, Point *clickedpoint)
{
  const Text *text = textobj->text;
  
  /* Set entries sensitive/selected etc here */
  textobj_menu_items[0].active = (text->numlines > 0) ? DIAMENU_ACTIVE : 0;

  return &textobj_menu;
}
